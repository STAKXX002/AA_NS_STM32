#!/usr/bin/env python3
"""
serial_test.py - Raspberry Pi 4B to STM32 UART5 Link Verification Tool
Part of AA_NS_STM32 project.

Verifies serial communication over UART5 (STM32 PC12/PD2 <-> Pi GPIO14/15).
Safe by default: blocks all motion and actuator commands unless --allow-motion is set.
"""

import argparse
import sys
import time
from typing import List, Optional, Tuple

try:
    import serial
except ImportError:
    print("Error: pyserial is required. Install via 'pip install pyserial' or 'pip install -r requirements.txt'.", file=sys.stderr)
    sys.exit(1)


# Commands that cause physical motion or actuator movement
MOTION_COMMANDS = {"HOME", "CAL", "MOVE", "OPEN", "CLOSE"}

# Exact line terminator expected by STM32 commands.c
# (STM32 ignores \r and triggers on \n)
TERMINATOR = b"\n"


class MotionCommandBlockedError(Exception):
    """Raised when a physical motion command is attempted without --allow-motion."""
    pass


class STM32SerialLink:
    """Manages serial connection and protocol interaction with the STM32 MCU."""

    def __init__(self, port: str = "/dev/serial0", baud: int = 115200, timeout: float = 2.0, allow_motion: bool = False):
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

    def is_motion_command(self, cmd: str) -> bool:
        """Checks whether a command initiates physical actuator motion."""
        parts = cmd.strip().split()
        if not parts:
            return False
        return parts[0].upper() in MOTION_COMMANDS

    def send_command(self, cmd: str, max_lines: int = 1, read_timeout: Optional[float] = None) -> Tuple[List[str], float]:
        """
        Sends a command string with line terminator and reads response lines.
        Returns (list_of_response_lines, latency_ms).
        """
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Serial port is not open.")

        cmd_clean = cmd.strip()
        if not self.allow_motion and self.is_motion_command(cmd_clean):
            raise MotionCommandBlockedError(
                f"Motion command '{cmd_clean}' blocked by default safety policy. Pass --allow-motion to enable."
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
        try:
            while len(lines) < max_lines:
                raw = self.ser.readline()
                if not raw:
                    break
                decoded = raw.decode("ascii", errors="replace").strip("\r\n")
                if decoded:
                    lines.append(decoded)
        finally:
            self.ser.timeout = orig_timeout

        latency_ms = (time.perf_counter() - start_time) * 1000.0
        return lines, latency_ms

    def ping(self) -> Tuple[bool, float, str]:
        """
        Sends 'PING' to verify link round-trip.
        STM32 firmware responds with 'UNKNOWN' (or ACK), confirming two-way communication.
        Returns (success, latency_ms, reply_text).
        """
        lines, latency_ms = self.send_command("PING", max_lines=1)
        if not lines:
            return False, latency_ms, "No response (timeout)"
        return True, latency_ms, lines[0]

    def status(self) -> Tuple[bool, List[str]]:
        """
        Queries status.
        Sends STATUS first; then queries POS for axis coordinates.
        Returns (success, response_lines).
        """
        # Send STATUS (as requested by spec)
        status_lines, _ = self.send_command("STATUS", max_lines=1)
        # Also query POS to retrieve detailed axis state
        pos_lines, _ = self.send_command("POS", max_lines=4, read_timeout=1.0)
        
        all_lines = []
        if status_lines:
            all_lines.append(f"STATUS reply: {status_lines[0]}")
        if pos_lines:
            all_lines.append("Axis Positions (POS):")
            all_lines.extend(f"  {line}" for line in pos_lines)
            
        if not all_lines:
            return False, ["No response received (timeout)"]
        return True, all_lines

    def loop(self, count: int = 10, interval: float = 0.1) -> Tuple[int, int, float, float, float]:
        """
        Performs N round-trip ping measurements.
        Returns (sent, received, min_lat_ms, avg_lat_ms, max_lat_ms).
        """
        latencies = []
        lost = 0

        print(f"--- Starting {count} PING round-trips to STM32 ({self.port_name} @ {self.baud} baud) ---")
        for i in range(1, count + 1):
            success, lat, reply = self.ping()
            if success:
                latencies.append(lat)
                print(f"[{i:3d}/{count}] Reply: '{reply}'  latency: {lat:6.2f} ms")
            else:
                lost += 1
                print(f"[{i:3d}/{count}] FAILED: {reply}")
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
        print(f"Motion commands enabled: {self.allow_motion}")
        print("Type commands (e.g. POS, ON, OFF, RST, STOP). Type 'exit' or 'quit' to leave.\n")

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
                # Read multiple response lines if available
                lines, latency_ms = self.send_command(user_input, max_lines=6, read_timeout=0.4)
                if not lines:
                    print(f"  [Timeout: no reply after {self.timeout}s]")
                else:
                    for line in lines:
                        print(f"  < {line}")
                    print(f"  (round-trip: {latency_ms:.1f} ms)")
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
        help="Explicitly permit motor/actuator commands (HOME, CAL, MOVE, OPEN, CLOSE)",
    )

    subparsers = parser.add_subparsers(dest="mode", help="Operating mode")

    # ping mode
    subparsers.add_parser("ping", help="Send single PING and report latency")

    # status mode
    subparsers.add_parser("status", help="Send STATUS and query axis coordinates")

    # loop mode
    loop_parser = subparsers.add_parser("loop", help="Perform N PING round-trips")
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
                success, latency, reply = link.ping()
                if success:
                    print(f"PING OK: reply='{reply}', latency={latency:.2f} ms")
                    return 0
                else:
                    print(f"PING FAILED: {reply}", file=sys.stderr)
                    return 1

            elif args.mode == "status":
                success, lines = link.status()
                if success:
                    print("--- STM32 Status ---")
                    for line in lines:
                        print(line)
                    return 0
                else:
                    print("STATUS FAILED: No response", file=sys.stderr)
                    return 1

            elif args.mode == "loop":
                sent, received, min_lat, avg_lat, max_lat = link.loop(
                    count=args.count, interval=args.interval
                )
                loss = sent - received
                loss_pct = (loss / sent) * 100.0 if sent else 100.0
                print("\n--- Ping Round-Trip Statistics ---")
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
