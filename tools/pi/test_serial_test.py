"""
test_serial_test.py - Unit tests for serial_test.py
Verifies command protocol handling, terminator behavior, guarded/unguarded command policy,
strict 4-line POS regex validation, raw hex formatting on framing/baud errors,
and parser probe check.
"""

import os
import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch
import pytest

# Ensure tools/pi directory is in sys.path
pi_tools_dir = Path(__file__).resolve().parent
if str(pi_tools_dir) not in sys.path:
    sys.path.insert(0, str(pi_tools_dir))

from serial_test import (
    STM32SerialLink,
    MotionCommandBlockedError,
    GUARDED_COMMANDS,
    UNGUARDED_COMMANDS,
    TERMINATOR,
    POS_LINE_RE,
    format_hex,
    validate_pos_lines,
)


VALID_POS_BYTES = [
    b"A1 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0\r\n",
    b"A2 UNHOMED POS 12.345 TRAVEL 50.000 MIN 1 MAX 0\r\n",
    b"A3 READY POS -5.200 TRAVEL 100.000 MIN 0 MAX 1\r\n",
    b"A4 MOVING POS 45.678 TRAVEL 200.000 MIN 0 MAX 0\r\n",
]

VALID_POS_LINES = [
    "A1 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0",
    "A2 UNHOMED POS 12.345 TRAVEL 50.000 MIN 1 MAX 0",
    "A3 READY POS -5.200 TRAVEL 100.000 MIN 0 MAX 1",
    "A4 MOVING POS 45.678 TRAVEL 200.000 MIN 0 MAX 0",
]


class TestProtocolAndSafety(unittest.TestCase):
    """Tests protocol rules, line terminators, and command guarding policy."""

    def test_terminator_is_newline(self):
        """STM32 commands.c triggers on \\n (ignores \\r)."""
        self.assertEqual(TERMINATOR, b"\n")

    def test_guarded_and_unguarded_classification(self):
        """Verifies that actuator/relay commands are guarded and safe queries/stops are unguarded."""
        link = STM32SerialLink(allow_motion=False)

        # All guarded commands
        expected_guarded = {"HOME", "CAL", "MOVE", "OPEN", "CLOSE", "ON", "OFF"}
        self.assertEqual(GUARDED_COMMANDS, expected_guarded)

        for cmd in expected_guarded:
            self.assertTrue(link.is_guarded_command(cmd))
            self.assertTrue(link.is_guarded_command(cmd.lower()))
            self.assertTrue(link.is_guarded_command(f"{cmd} 1 100"))

        # Unguarded safe commands
        for cmd in UNGUARDED_COMMANDS:
            self.assertFalse(link.is_guarded_command(cmd))
            self.assertFalse(link.is_guarded_command(cmd.lower()))

    def test_guarded_commands_blocked_by_default(self):
        """Actuator/relay commands must raise MotionCommandBlockedError when allow_motion=False."""
        link = STM32SerialLink(allow_motion=False)
        link.ser = MagicMock()
        link.ser.is_open = True

        guarded_attempts = [
            "HOME",
            "CAL",
            "MOVE A1 100",
            "OPEN",
            "CLOSE",
            "ON",
            "OFF",
        ]
        for cmd in guarded_attempts:
            with self.assertRaises(MotionCommandBlockedError):
                link.send_command(cmd)

        link.ser.write.assert_not_called()

    def test_guarded_commands_allowed_when_enabled(self):
        """Actuator/relay commands succeed when allow_motion=True."""
        link = STM32SerialLink(allow_motion=True)
        link.ser = MagicMock()
        link.ser.is_open = True
        link.ser.readline.return_value = b"OK\r\n"

        for cmd in ["MOVE A1 100", "ON", "OFF"]:
            lines, raw, lat = link.send_command(cmd)
            self.assertEqual(lines, ["OK"])

    def test_unguarded_commands_allowed_by_default(self):
        """POS, STOP, HALT, and RST must be permitted without allow_motion."""
        link = STM32SerialLink(allow_motion=False)
        link.ser = MagicMock()
        link.ser.is_open = True
        link.ser.readline.return_value = b"OK\r\n"

        for cmd in ["POS", "STOP", "HALT", "RST"]:
            lines, raw, lat = link.send_command(cmd)
            self.assertEqual(lines, ["OK"])


class TestPosValidationAndHexFormatting(unittest.TestCase):
    """Tests regex validation and hex formatting on diverse replies."""

    def test_pos_regex_matching(self):
        """Validates regex on standard firmware outputs."""
        for line in VALID_POS_LINES:
            m = POS_LINE_RE.match(line)
            self.assertIsNotNone(m, f"Failed to match valid line: {line}")

        # Invalid lines
        self.assertIsNone(POS_LINE_RE.match("UNKNOWN"))
        self.assertIsNone(POS_LINE_RE.match("A5 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0"))
        self.assertIsNone(POS_LINE_RE.match("A1 UNHOMED POS abc TRAVEL 0.000 MIN 0 MAX 0"))

    def test_validate_pos_lines_helper(self):
        """Verifies validate_pos_lines helper function."""
        is_valid, err = validate_pos_lines(VALID_POS_LINES)
        self.assertTrue(is_valid)
        self.assertIsNone(err)

        # Less than 4 lines
        is_valid, err = validate_pos_lines(VALID_POS_LINES[:3])
        self.assertFalse(is_valid)
        self.assertIn("Expected 4 lines", err)

        # Swapped axis order
        swapped = [VALID_POS_LINES[1], VALID_POS_LINES[0], VALID_POS_LINES[2], VALID_POS_LINES[3]]
        is_valid, err = validate_pos_lines(swapped)
        self.assertFalse(is_valid)
        self.assertIn("wrong axis ID", err)

    def test_format_hex(self):
        """Verifies byte sequence formatting into spaced uppercase hex."""
        self.assertEqual(format_hex(b""), "<empty (no bytes received)>")
        self.assertEqual(format_hex(b"UNKNOWN\r\n"), "55 4E 4B 4E 4F 57 4E 0D 0A")
        self.assertEqual(format_hex(b"\xff\x00\xaa"), "FF 00 AA")


class TestSerialLinkInteractions(unittest.TestCase):
    """Tests ping, probe, status, and loop against mock serial responses."""

    def setUp(self):
        self.link = STM32SerialLink(port="/dev/test_port", baud=115200, timeout=1.0)
        self.mock_ser = MagicMock()
        self.mock_ser.is_open = True
        self.link.ser = self.mock_ser

    def test_ping_valid_pos(self):
        """Ping sending POS and receiving 4 valid axis lines succeeds."""
        self.mock_ser.readline.side_effect = list(VALID_POS_BYTES)
        success, lat, lines, raw, msg = self.link.ping()

        self.assertTrue(success)
        self.assertEqual(len(lines), 4)
        self.assertEqual(msg, "4 axes verified (POS)")
        self.mock_ser.write.assert_called_once_with(b"POS\n")

    def test_ping_unknown_fails_with_hex(self):
        """Ping receiving UNKNOWN fails and returns raw hex for diagnostics."""
        self.mock_ser.readline.side_effect = [b"UNKNOWN\r\n", b""]
        success, lat, lines, raw, msg = self.link.ping()

        self.assertFalse(success)
        self.assertEqual(lines, ["UNKNOWN"])
        self.assertEqual(format_hex(raw), "55 4E 4B 4E 4F 57 4E 0D 0A")
        self.assertIn("expected 4 lines", msg.lower())

    def test_ping_garbage_fails_with_hex(self):
        """Ping receiving framing/baud garbage fails with hex dump."""
        garbage_bytes = b"\xff\xfe\x00\xaa\r\n"
        self.mock_ser.readline.side_effect = [garbage_bytes, b""]
        success, lat, lines, raw, msg = self.link.ping()

        self.assertFalse(success)
        self.assertEqual(format_hex(raw), "FF FE 00 AA 0D 0A")
        self.assertIn("expected 4 lines", msg.lower())

    def test_ping_invalid_line_format_fails(self):
        """Ping receiving 4 lines where one line has invalid format fails."""
        invalid_four_lines = [
            VALID_POS_BYTES[0],
            b"GARBAGE_LINE_2\r\n",
            VALID_POS_BYTES[2],
            VALID_POS_BYTES[3],
        ]
        self.mock_ser.readline.side_effect = invalid_four_lines
        success, lat, lines, raw, msg = self.link.ping()

        self.assertFalse(success)
        self.assertIn("invalid format", msg.lower())

    def test_ping_short_reply_fails(self):
        """Ping receiving fewer than 4 lines before timeout fails."""
        self.mock_ser.readline.side_effect = [VALID_POS_BYTES[0], VALID_POS_BYTES[1], b""]
        success, lat, lines, raw, msg = self.link.ping()

        self.assertFalse(success)
        self.assertEqual(len(lines), 2)
        self.assertIn("Expected 4 lines for POS, received 2", msg)

    def test_ping_timeout_fails(self):
        """Ping receiving zero bytes fails with timeout report."""
        self.mock_ser.readline.return_value = b""
        success, lat, lines, raw, msg = self.link.ping()

        self.assertFalse(success)
        self.assertEqual(len(lines), 0)
        self.assertEqual(format_hex(raw), "<empty (no bytes received)>")
        self.assertIn("Expected 4 lines for POS, received 0", msg)

    def test_probe_parser_check(self):
        """Probe sends PING and verifies the MCU parser alone replies UNKNOWN."""
        # Success case: returns UNKNOWN
        self.mock_ser.readline.return_value = b"UNKNOWN\r\n"
        success, lat, reply, raw, msg = self.link.probe()
        self.assertTrue(success)
        self.assertEqual(reply, "UNKNOWN")
        self.mock_ser.write.assert_called_with(b"PING\n")

        # Failure case: returns unexpected text
        self.mock_ser.readline.return_value = b"OK\r\n"
        success, lat, reply, raw, msg = self.link.probe()
        self.assertFalse(success)
        self.assertIn("Expected 'UNKNOWN', got 'OK'", msg)

        # Failure case: timeout
        self.mock_ser.readline.return_value = b""
        success, lat, reply, raw, msg = self.link.probe()
        self.assertFalse(success)
        self.assertIn("timeout", msg.lower())

    def test_status_parsing(self):
        """Status parses 4 valid POS lines into structured dictionary records."""
        self.mock_ser.readline.side_effect = list(VALID_POS_BYTES)
        success, axes, raw, msg = self.link.status()

        self.assertTrue(success)
        self.assertEqual(len(axes), 4)
        self.assertEqual(axes[0]["axis"], "A1")
        self.assertEqual(axes[0]["state"], "UNHOMED")
        self.assertAlmostEqual(axes[0]["pos"], 0.0)
        self.assertEqual(axes[1]["axis"], "A2")
        self.assertAlmostEqual(axes[1]["pos"], 12.345)
        self.assertEqual(axes[1]["min_switch"], 1)

    def test_status_invalid_reply(self):
        """Status receiving UNKNOWN or garbage fails with diagnostic message."""
        self.mock_ser.readline.side_effect = [b"UNKNOWN\r\n", b""]
        success, axes, raw, msg = self.link.status()

        self.assertFalse(success)
        self.assertEqual(axes, [])
        self.assertEqual(format_hex(raw), "55 4E 4B 4E 4F 57 4E 0D 0A")

    def test_loop_statistics_and_hex(self):
        """Loop executes N pings and records loss and latencies."""
        ping_side_effects = [
            (True, 12.0, VALID_POS_LINES, b"".join(VALID_POS_BYTES), "OK"),
            (False, 0.0, ["UNKNOWN"], b"UNKNOWN\r\n", "Line 1 invalid format"),
            (True, 18.0, VALID_POS_LINES, b"".join(VALID_POS_BYTES), "OK"),
        ]
        with patch.object(self.link, "ping", side_effect=ping_side_effects):
            sent, received, min_lat, avg_lat, max_lat = self.link.loop(count=3, interval=0.0)

            self.assertEqual(sent, 3)
            self.assertEqual(received, 2)
            self.assertAlmostEqual(min_lat, 12.0)
            self.assertAlmostEqual(avg_lat, 15.0)
            self.assertAlmostEqual(max_lat, 18.0)
