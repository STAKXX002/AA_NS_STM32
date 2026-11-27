#!/usr/bin/env python3
"""
serial_test.py - Raspberry Pi 4B to STM32 UART5 Link Verification Tool
Part of AA_NS_STM32 project.

Verifies serial communication over UART5 (STM32 PC12/PD2 <-> Pi GPIO14/15).
Safe by default: blocks all motion and actuator/relay commands unless --allow-motion is set.
"""

import argparse
import re
import sys
import time
from typing import Any, Dict, List, Optional, Tuple

try:
    import serial
except ImportError:
    print(
        "Error: pyserial is required. Install via 'pip install pyserial' or 'pip install -r requirements.txt'.",
        file=sys.stderr,
    )
    sys.exit(1)


# Commands that cause physical motion or actuator/relay actuation
GUARDED_COMMANDS = {"HOME", "CAL", "MOVE", "OPEN", "CLOSE", "ON", "OFF"}
# Backward compatibility alias
MOTION_COMMANDS = GUARDED_COMMANDS

# Unguarded commands (safe read-only, emergency stops, resets)
UNGUARDED_COMMANDS = {"POS", "STOP", "HALT", "RST"}

# Exact line terminator expected by STM32 commands.c
# (STM32 ignores \r and triggers on \n)
TERMINATOR = b"\n"

# Regex matching valid POS line:
# "A<n> <STATE> POS <f> TRAVEL <f> MIN <0|1> MAX <0|1>"
# Example: A1 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0
POS_LINE_RE = re.compile(
    r"^A(?P<axis>[1-4])\s+(?P<state>[A-Za-z0-9_?]+)\s+POS\s+(?P<pos>-?\d+(?:\.\d+)?)\s+TRAVEL\s+(?P<travel>-?\d+(?:\.\d+)?)\s+MIN\s+(?P<min>[01])\s+MAX\s+(?P<max>[01])$"
)


class MotionCommandBlockedError(Exception):
    """Raised when an actuator, motion, or relay command is attempted without --allow-motion."""
    pass


def format_hex(data: bytes) -> str:
    """Formats bytes as spaced uppercase hex pairs, e.g. '55 4E 4B 4E 4F 57 4E 0D 0A'."""
    if not data:
        return "<empty (no bytes received)>"
    return " ".join(f"{b:02X}" for b in data)


def validate_pos_lines(lines: List[str]) -> Tuple[bool, Optional[str]]:
    """
    Validates that response lines contain exactly four valid axis lines (A1..A4).
    Returns (is_valid, error_description).
    """
    if len(lines) != 4:
        detail = f" ({lines[0]!r})" if len(lines) == 1 else ""
        return False, f"Expected 4 lines for POS, received {len(lines)}{detail}"

    for expected_axis, line in enumerate(lines, start=1):
        m = POS_LINE_RE.match(line.strip())
        if not m:
            return False, f"Line {expected_axis} invalid format: {line!r}"
        if int(m.group("axis")) != expected_axis:
            return False, f"Line {expected_axis} has wrong axis ID (expected A{expected_axis}, got A{m.group('axis')})"

    return True, None


class STM32SerialLink:
    """Manages serial connection and protocol interaction with the STM32 MCU."""

    def __init__(
        self,
        port: str = "/dev/serial0",
        baud: int = 115200,
        timeout: float = 2.0,
        allow_motion: bool = False,
    ):
        self.port_name = port
        self.baud = baud
        self.timeout = timeout
        self.allow_motion = allow_motion
        self.ser: Optional[serial.Serial] = None

    def open(self) -> None:
        """Opens the serial port."""
        try:
            self.ser = serial.Serial(
                port=self.port_name,
                baudrate=self.baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.timeout,
            )
            # Allow UART hardware and USB adapters a brief settling time
            time.sleep(0.05)
            self.flush()
        except serial.SerialException as e:
            raise RuntimeError(f"Failed to open serial port '{self.port_name}': {e}") from e

    def close(self) -> None:
        """Closes the serial port if open."""
        if self.ser and self.ser.is_open:
            self.ser.close()
            self.ser = None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def flush(self) -> None:
        """Flushes input and output buffers."""
        if self.ser and self.ser.is_open:
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()

    def is_guarded_command(self, cmd: str) -> bool:
        """Checks whether a command actuates physical motion or relays."""
        parts = cmd.strip().split()
        if not parts:
            return False
        return parts[0].upper() in GUARDED_COMMANDS

    def is_motion_command(self, cmd: str) -> bool:
        """Alias for is_guarded_command (backward compatibility)."""
        return self.is_guarded_command(cmd)

    def send_command(
        self,
        cmd: str,
        max_lines: int = 1,
        read_timeout: Optional[float] = None,
    ) -> Tuple[List[str], bytes, float]:
        """
        Sends a command string with line terminator and reads response lines.
        Returns (list_of_decoded_lines, raw_bytes, latency_ms).
        """
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Serial port is not open.")

        cmd_clean = cmd.strip()
        if not self.allow_motion and self.is_guarded_command(cmd_clean):
            raise MotionCommandBlockedError(
                f"Command '{cmd_clean}' blocked by default safety policy (motion/relay command). Pass --allow-motion to enable."
            )

        # Flush stale incoming bytes before sending
        self.flush()

        wire_bytes = cmd_clean.encode("ascii", errors="replace") + TERMINATOR
        start_time = time.perf_counter()
        self.ser.write(wire_bytes)
        self.ser.flush()

        effective_timeout = read_timeout if read_timeout is not None else self.timeout
        orig_timeout = self.ser.timeout
        self.ser.timeout = effective_timeout

        lines = []
        raw_bytes = bytearray()
        try:
            while len(lines) < max_lines:
                raw = self.ser.readline()
                if not raw:
                    break
                raw_bytes.extend(raw)
                decoded = raw.decode("ascii", errors="replace").strip("\r\n")
                if decoded:
                    lines.append(decoded)
                    # If we received UNKNOWN on a multi-line query, the MCU has completed its response
                    if decoded == "UNKNOWN":
                        break
        finally:
            self.ser.timeout = orig_timeout

        latency_ms = (time.perf_counter() - start_time) * 1000.0
        return lines, bytes(raw_bytes), latency_ms

    def ping(self) -> Tuple[bool, float, List[str], bytes, str]:
        """
        Sends 'POS' (read-only query) to verify two-way communication.
        Requires full reply to match four valid axis lines (A1..A4).
        Returns (success, latency_ms, lines, raw_bytes, message).
        """
        lines, raw_bytes, latency_ms = self.send_command("POS", max_lines=4)
        is_valid, err = validate_pos_lines(lines)
        if not is_valid:
            return False, latency_ms, lines, raw_bytes, (err or "Validation failed")
        return True, latency_ms, lines, raw_bytes, "4 axes verified (POS)"

    def probe(self) -> Tuple[bool, float, str, bytes, str]:
        """
        Sends unmapped token 'PING' to verify the MCU command parser alone.
        Requires exact reply 'UNKNOWN'.
        Returns (success, latency_ms, reply_text, raw_bytes, message).
        """
        lines, raw_bytes, latency_ms = self.send_command("PING", max_lines=1)
        if not lines:
            return False, latency_ms, "", raw_bytes, "No response (timeout)"
        if lines[0] != "UNKNOWN":
            return False, latency_ms, lines[0], raw_bytes, f"Expected 'UNKNOWN', got {lines[0]!r}"
        return True, latency_ms, lines[0], raw_bytes, "Parser replied 'UNKNOWN'"

    def status(self) -> Tuple[bool, List[Dict[str, Any]], bytes, str]:
        """
        Queries status via 'POS' and parses the axis table.
        Returns (success, parsed_axis_dicts, raw_bytes, message).
        """
        lines, raw_bytes, _ = self.send_command("POS", max_lines=4)
        is_valid, err = validate_pos_lines(lines)
        if not is_valid:
            return False, [], raw_bytes, (err or "Invalid POS response")

        parsed_axes = []
        for line in lines:
            m = POS_LINE_RE.match(line.strip())
            if m:
                parsed_axes.append({
                    "axis": f"A{m.group('axis')}",
                    "state": m.group("state"),
                    "pos": float(m.group("pos")),
                    "travel": float(m.group("travel")),
                    "min_switch": int(m.group("min")),
                    "max_switch": int(m.group("max")),
                    "raw": line,
                })
        return True, parsed_axes, raw_bytes, "Status OK"

    def loop(self, count: int = 10, interval: float = 0.1) -> Tuple[int, int, float, float, float]:
        """
        Performs N POS round-trip measurements with strict 4-line reply validation.
        Returns (sent, received, min_lat_ms, avg_lat_ms, max_lat_ms).
        """
        latencies = []
        lost = 0

        print(f"--- Starting {count} POS round-trips to STM32 ({self.port_name} @ {self.baud} baud) ---")
        for i in range(1, count + 1):
            success, lat, lines, raw_bytes, msg = self.ping()
            if success:
                latencies.append(lat)
                print(f"[{i:3d}/{count}] OK: 4 axes verified (POS)  latency: {lat:6.2f} ms")
            else:
                lost += 1
                hex_str = format_hex(raw_bytes)
                print(f"[{i:3d}/{count}] FAILED: {msg} | hex: {hex_str}")
            if i < count and interval > 0:
                time.sleep(interval)

        sent = count
        received = len(latencies)
        min_lat = min(latencies) if latencies else 0.0
        max_lat = max(latencies) if latencies else 0.0
        avg_lat = sum(latencies) / received if received else 0.0

        return sent, received, min_lat, avg_lat, max_lat

    def interactive(self) -> None:
        """Interactive terminal console to send arbitrary commands to STM32."""
        print(f"=== STM32 Interactive Console ({self.port_name} @ {self.baud}) ===")
        print(f"Guarded commands enabled (--allow-motion): {self.allow_motion}")
        print("Unguarded: POS, STOP, HALT, RST. Guarded: HOME, CAL, MOVE, OPEN, CLOSE, ON, OFF.")
        print("Type commands (or 'exit' / 'quit' to leave).\n")

        while True:
            try:
                user_input = input("STM32> ").strip()
            except (EOFError, KeyboardInterrupt):
                print("\nExiting.")
                break

            if not user_input:
                continue
            if user_input.lower() in ("exit", "quit"):
                break

            try:
                lines, raw_bytes, latency_ms = self.send_command(user_input, max_lines=6, read_timeout=0.4)
                if not lines:
                    print(f"  [Timeout: no reply after {self.timeout}s | raw hex: {format_hex(raw_bytes)}]")
                else:
                    for line in lines:
                        print(f"  < {line}")
                    print(f"  (round-trip: {latency_ms:.1f} ms | hex: {format_hex(raw_bytes)})")
            except MotionCommandBlockedError as e:
                print(f"  [BLOCKED] {e}")
            except Exception as e:
                print(f"  [ERROR] {e}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Raspberry Pi 4B <-> STM32 UART5 Serial Link Test Tool"
    )
    parser.add_argument(
        "--port",
        default="/dev/serial0",
        help="Serial device path (default: /dev/serial0)",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Baud rate (default: 115200)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=2.0,
        help="Read timeout in seconds (default: 2.0)",
    )
    parser.add_argument(
        "--allow-motion",
        action="store_true",
        help="Explicitly permit motion and actuator/relay commands (HOME, CAL, MOVE, OPEN, CLOSE, ON, OFF)",
    )

    subparsers = parser.add_subparsers(dest="mode", help="Operating mode")

    # ping mode
    subparsers.add_parser("ping", help="Send POS (read-only) and verify 4-axis status reply")

    # probe mode
    subparsers.add_parser("probe", help="Send unmapped PING token to verify MCU parser (expects UNKNOWN)")

    # status mode
    subparsers.add_parser("status", help="Print parsed 4-axis POS table")

    # loop mode
    loop_parser = subparsers.add_parser("loop", help="Perform N POS round-trips with 4-line reply validation")
    loop_parser.add_argument(
        "count",
        type=int,
        nargs="?",
        default=10,
        help="Number of ping iterations (default: 10)",
    )
    loop_parser.add_argument(
        "--interval",
        type=float,
        default=0.1,
        help="Interval between pings in seconds (default: 0.1)",
    )

    # interactive mode
    subparsers.add_parser("interactive", help="Start interactive serial terminal")

    args = parser.parse_args()

    if not args.mode:
        parser.print_help()
        return 1

    try:
        with STM32SerialLink(
            port=args.port,
            baud=args.baud,
            timeout=args.timeout,
            allow_motion=args.allow_motion,
        ) as link:

            if args.mode == "ping":
                success, latency, lines, raw_bytes, msg = link.ping()
                if success:
                    print(f"PING OK: 4 axes verified (POS), latency={latency:.2f} ms")
                    for line in lines:
                        print(f"  {line}")
                    return 0
                else:
                    print(f"PING FAILED: {msg}", file=sys.stderr)
                    print(f"  Raw received (hex): {format_hex(raw_bytes)}", file=sys.stderr)
                    if lines:
                        print(f"  Decoded text: {lines!r}", file=sys.stderr)
                    return 1

            elif args.mode == "probe":
                success, latency, reply, raw_bytes, msg = link.probe()
                if success:
                    print(f"PARSER CHECK OK: MCU command parser replied '{reply}', latency={latency:.2f} ms")
                    return 0
                else:
                    print(f"PARSER CHECK FAILED: {msg}", file=sys.stderr)
                    print(f"  Raw received (hex): {format_hex(raw_bytes)}", file=sys.stderr)
                    return 1

            elif args.mode == "status":
                success, axes, raw_bytes, msg = link.status()
                if success:
                    print("--- STM32 Axis Status (POS) ---")
                    header = f"{'Axis':<6} {'State':<10} {'Pos (mm)':<12} {'Travel (mm)':<12} {'Min Sw':<8} {'Max Sw':<8}"
                    print(header)
                    print("-" * len(header))
                    for a in axes:
                        print(
                            f"{a['axis']:<6} {a['state']:<10} {a['pos']:<12.3f} {a['travel']:<12.3f} "
                            f"{a['min_switch']:<8} {a['max_switch']:<8}"
                        )
                    return 0
                else:
                    print(f"STATUS FAILED: {msg}", file=sys.stderr)
                    print(f"  Raw received (hex): {format_hex(raw_bytes)}", file=sys.stderr)
                    return 1

            elif args.mode == "loop":
                sent, received, min_lat, avg_lat, max_lat = link.loop(
                    count=args.count, interval=args.interval
                )
                loss = sent - received
                loss_pct = (loss / sent) * 100.0 if sent else 100.0
                print("\n--- Ping Round-Trip Statistics (POS) ---")
                print(f"Packets: Sent = {sent}, Received = {received}, Lost = {loss} ({loss_pct:.1f}% loss)")
                if received > 0:
                    print(f"Latency: Min = {min_lat:.2f} ms, Avg = {avg_lat:.2f} ms, Max = {max_lat:.2f} ms")
                return 0 if (received > 0 and loss == 0) else 1

            elif args.mode == "interactive":
                link.interactive()
                return 0

    except RuntimeError as e:
        print(f"Serial Error: {e}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nAborted by user.")
        return 130

    return 0


if __name__ == "__main__":
    sys.exit(main())
