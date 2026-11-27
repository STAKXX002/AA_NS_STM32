"""
test_serial_test.py - Unit tests for serial_test.py
Verifies command protocol handling, terminator behavior, motion safety guards,
and mock serial execution for the Raspberry Pi UART5 verification tool.
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
    MOTION_COMMANDS,
    TERMINATOR,
)


class TestProtocolAndSafety(unittest.TestCase):
    """Tests protocol rules, terminators, and motion safety guards."""

    def test_terminator_is_newline(self):
        """STM32 commands.c triggers only on \\n (ignores \\r)."""
        self.assertEqual(TERMINATOR, b"\n")

    def test_motion_command_detection(self):
        """Verifies that all motion/actuator commands are detected."""
        link = STM32SerialLink(allow_motion=False)

        # Base motion commands
        for cmd in MOTION_COMMANDS:
            self.assertTrue(link.is_motion_command(cmd))
            self.assertTrue(link.is_motion_command(cmd.lower()))
            self.assertTrue(link.is_motion_command(f"{cmd} A1 100 500"))
            self.assertTrue(link.is_motion_command(f"  {cmd.lower()} 123  "))

        # Non-motion commands should not be flagged as motion commands
        safe_commands = [
            "PING",
            "STATUS",
            "POS",
            "ON",
            "OFF",
            "STOP",
            "HALT",
            "RST",
            "UNKNOWN",
        ]
        for cmd in safe_commands:
            self.assertFalse(link.is_motion_command(cmd))

    def test_motion_blocked_by_default(self):
        """Motion commands must raise MotionCommandBlockedError when allow_motion=False."""
        link = STM32SerialLink(allow_motion=False)
        link.ser = MagicMock()
        link.ser.is_open = True

        for cmd in ["HOME", "MOVE A1 100", "CAL", "OPEN", "CLOSE"]:
            with self.assertRaises(MotionCommandBlockedError):
                link.send_command(cmd)

        # Verify nothing was written to serial port
        link.ser.write.assert_not_called()

    def test_motion_allowed_when_explicitly_enabled(self):
        """Motion commands succeed when allow_motion=True."""
        link = STM32SerialLink(allow_motion=True)
        link.ser = MagicMock()
        link.ser.is_open = True
        link.ser.readline.return_value = b"OK\r\n"

        lines, latency = link.send_command("MOVE A1 100")
        self.assertEqual(lines, ["OK"])
        link.ser.write.assert_called_once_with(b"MOVE A1 100\n")


class TestSerialLinkInteraction(unittest.TestCase):
    """Tests command transmission, buffer flushing, and response handling using mock serial."""

    def setUp(self):
        self.link = STM32SerialLink(port="/dev/test_port", baud=115200, timeout=1.0)
        self.mock_ser = MagicMock()
        self.mock_ser.is_open = True
        self.link.ser = self.mock_ser

    def test_flush_before_write(self):
        """Verifies that input and output buffers are cleared prior to write."""
        self.mock_ser.readline.return_value = b"UNKNOWN\r\n"
        self.link.send_command("PING")

        self.mock_ser.reset_input_buffer.assert_called()
        self.mock_ser.reset_output_buffer.assert_called()
        self.mock_ser.write.assert_called_once_with(b"PING\n")

    def test_ping_success(self):
        """Test ping when STM32 responds with UNKNOWN (expected standard response)."""
        self.mock_ser.readline.return_value = b"UNKNOWN\r\n"
        success, latency, reply = self.link.ping()

        self.assertTrue(success)
        self.assertEqual(reply, "UNKNOWN")
        self.assertGreater(latency, 0.0)

    def test_ping_timeout(self):
        """Test ping when STM32 does not reply (empty readline)."""
        self.mock_ser.readline.return_value = b""
        success, latency, reply = self.link.ping()

        self.assertFalse(success)
        self.assertIn("timeout", reply.lower())

    def test_status_parsing(self):
        """Test status mode querying STATUS followed by POS."""
        def mock_readline_side_effect():
            # First query is STATUS -> UNKNOWN
            # Second query is POS -> axis lines followed by empty string
            responses = [
                b"UNKNOWN\r\n",
                b"A1 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0\r\n",
                b"A2 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0\r\n",
                b"",
            ]
            for r in responses:
                yield r

        gen = mock_readline_side_effect()
        self.mock_ser.readline.side_effect = lambda: next(gen)

        success, lines = self.link.status()
        self.assertTrue(success)
        self.assertTrue(any("STATUS reply: UNKNOWN" in l for l in lines))
        self.assertTrue(any("A1 UNHOMED" in l for l in lines))

    def test_loop_statistics(self):
        """Test loop calculation of min, avg, max, and loss counts."""
        # Simulate 3 successful pings and 1 failed ping
        ping_returns = [
            (True, 10.0, "UNKNOWN"),
            (True, 20.0, "UNKNOWN"),
            (False, 0.0, "No response"),
            (True, 30.0, "UNKNOWN"),
        ]
        with patch.object(self.link, "ping", side_effect=ping_returns):
            sent, received, min_lat, avg_lat, max_lat = self.link.loop(count=4, interval=0.0)

            self.assertEqual(sent, 4)
            self.assertEqual(received, 3)
            self.assertAlmostEqual(min_lat, 10.0)
            self.assertAlmostEqual(avg_lat, 20.0)
            self.assertAlmostEqual(max_lat, 30.0)
