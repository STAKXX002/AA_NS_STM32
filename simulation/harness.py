"""Renode test harness: boots the firmware and talks to it over the UART socket.

One `Sim` instance == one fresh Renode process. Renode's own console output
goes to simulation/logs/<test-name>.log, never to the terminal.
"""
import contextlib
import os
import re
import signal
import socket
import time

import pexpect

HOST = '127.0.0.1'
UART_PORT = 1234  # must match `CreateServerSocketTerminal` in run_stm32.resc

SIM_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SIM_DIR)
LOG_DIR = os.path.join(SIM_DIR, 'logs')
SIM_ELF = os.path.join(ROOT, 'build', 'Simulation', 'AA_STM.elf')
PROMPT = r'\(stm32f446\)'

# Renode simulates the 1 kHz step stream roughly 10x slower than real time.
# Renode runs the 1 kHz step stream several times slower than real time, so
# timeouts are generous. NOTE: these were carried over from the Swapping
# firmware and NOT re-measured for AA - tune on first run. On a slower machine:
#     SIM_TIMEOUT_SCALE=2 pytest simulation
TIMEOUT_SCALE = float(os.environ.get('SIM_TIMEOUT_SCALE', '1'))
T_HOME, T_MOVE, T_HATCH = 60.0, 150.0, 120.0

ANSI_RE = re.compile(r'\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Za-z0-9]')
CTRL_RE = re.compile(r'[^\x20-\x7e\n]')  # printable ASCII + newline only


def clean(text):
  """Strips ANSI codes, carriage returns and stray control bytes."""
  return CTRL_RE.sub('', ANSI_RE.sub('', text).replace('\r', ''))


class SimTimeout(AssertionError):
  """Expected UART text did not arrive in time (reported as a test failure)."""


class Sim:

  def __init__(self, name='sim'):
    self.name = re.sub(r'[^A-Za-z0-9_.-]', '_', name)
    self.log_path = os.path.join(LOG_DIR, f'{self.name}.log')
    self.renode = None
    self.sock = None
    self.rx = ''          # UART text since the last uart() call
    self.history = []     # every COMPLETE UART line of this run
    self._partial = ''    # current line that has not seen its newline yet
    self._tail = b''      # incomplete telnet sequence carried between recv()s
    self._log = None

  # ------------------------------------------------------------ lifecycle
  @staticmethod
  def _port_in_use():
    with socket.socket() as s:
      s.settimeout(0.2)
      return s.connect_ex((HOST, UART_PORT)) == 0

  def _wait_port_free(self, timeout=10.0):
    end = time.time() + timeout
    while self._port_in_use():
      if time.time() > end:
        raise RuntimeError(
            f'port {UART_PORT} is busy - a stale Renode is probably running '
            '(try: pkill -f renode)')
      time.sleep(0.2)

  def start(self, boot_timeout=15.0):
    if not os.path.exists(SIM_ELF):
      raise RuntimeError(f'{SIM_ELF} not found - build the Simulation preset')
    os.makedirs(LOG_DIR, exist_ok=True)
    self._wait_port_free()
    self._log = open(self.log_path, 'w')
    self.renode = pexpect.spawn(
        'renode --disable-gui --console --plain '
        'simulation/scripts/run_stm32.resc',
        cwd=ROOT, encoding='utf-8', timeout=boot_timeout)
    self.renode.logfile_read = self._log
    self.renode.expect(PROMPT)

    # Connect as soon as the server socket exists (no fixed sleep).
    end = time.time() + 10.0
    while True:
      try:
        self.sock = socket.create_connection((HOST, UART_PORT), timeout=1.0)
        break
      except OSError:
        if time.time() > end:
          raise
        time.sleep(0.1)
    self.sock.settimeout(0.2)
    self.expect('HOME REQUIRED', timeout=boot_timeout)

  def cleanup(self):
    if self.sock:
      with contextlib.suppress(Exception):
        self.sock.close()
    if self.renode:
      with contextlib.suppress(Exception):
        self.renode.sendline('quit')
        time.sleep(0.3)
      if self.renode.isalive():
        try:
          os.killpg(os.getpgid(self.renode.pid), signal.SIGKILL)
        except Exception:
          self.renode.close(force=True)
      with contextlib.suppress(Exception):
        self.renode.wait()
    if self._log:
      with contextlib.suppress(Exception):
        self._log.close()

  # --------------------------------------------------------------- Renode
  def renode_cmd(self, cmd):
    """Runs a Renode monitor command and waits for the prompt."""
    self.renode.sendline(cmd)
    self.renode.expect(PROMPT)

  # (port, pin) of each endstop - keep in sync with the table in Core/Src/axis.c
  ENDSTOPS = {
      (1, 'min'): ('B', 0),  (1, 'max'): ('C', 1),
      (2, 'min'): ('A', 7),  (2, 'max'): ('A', 6),
      (3, 'min'): ('B', 10), (3, 'max'): ('A', 8),
      (4, 'min'): ('C', 9),  (4, 'max'): ('B', 8),
  }

  def _switch(self, axis, which, pressed):
    port, pin = self.ENDSTOPS[(axis, which)]
    # active-low: pressed = pin driven low
    return f'sysbus.gpioPort{port} OnGPIO {pin} {"false" if pressed else "true"}'

  def press(self, *switches):
    """Presses endstops, e.g. press((1, 'min'), (2, 'max')), in ONE monitor
    line so the pins change together with no skew gap."""
    self.renode_cmd('; '.join(self._switch(a, w, True) for a, w in switches))

  def release(self, *switches):
    """Releases the given endstops (no arguments: releases all eight)."""
    switches = switches or tuple(self.ENDSTOPS)
    self.renode_cmd('; '.join(self._switch(a, w, False) for a, w in switches))

  # ----------------------------------------------------------------- UART
  def uart(self, cmd):
    self.rx = ''
    self.sock.sendall(f'{cmd}\n'.encode())

  def _filter(self, data):
    """Drops telnet negotiation (IAC ...) that Renode's socket terminal sends
    on connect - that is where a stray '"' before READY came from."""
    buf = self._tail + data
    out, i, n = bytearray(), 0, len(buf)
    while i < n:
      b = buf[i]
      if b != 0xFF:
        out.append(b)
        i += 1
        continue
      if i + 1 >= n:
        break  # incomplete, wait for more
      c = buf[i + 1]
      if 0xFB <= c <= 0xFE:  # WILL/WONT/DO/DONT <option>
        if i + 2 >= n:
          break
        i += 3
      else:
        i += 2
    self._tail = bytes(buf[i:])
    return out.decode('ascii', 'ignore')

  def _pump(self, wait):
    """Reads whatever arrives within `wait` seconds into self.rx."""
    try:
      data = self.sock.recv(1024)
    except socket.timeout:
      return False
    if not data:
      raise SimTimeout('UART socket closed by Renode')
    text = clean(self._filter(data))
    self.rx += text
    # Renode's UART delivers a few bytes at a time, so a line such as READY
    # can arrive split over several reads: only record whole lines.
    self._partial += text
    *lines, self._partial = self._partial.split('\n')
    self.history.extend(l.strip() for l in lines if l.strip())
    return True

  @property
  def transcript(self):
    """Everything received so far, including a line still in progress."""
    return '\n'.join(self.history + ([self._partial.strip()]
                                     if self._partial.strip() else []))

  def expect(self, substring, timeout=20.0):
    """Waits until `substring` appears in the UART text since the last
    uart() call. Returns all text received so far."""
    timeout *= TIMEOUT_SCALE
    end = time.time() + timeout
    while substring not in self.rx:
      if time.time() > end:
        recent = self.history[-8:] + ([self._partial.strip()]
                                      if self._partial.strip() else [])
        last = '\n    '.join(recent) or '(nothing received)'
        raise SimTimeout(
            f"timeout waiting for {substring!r} after {timeout:.0f}s\n"
            f"  last UART lines:\n    {last}\n"
            f"  Renode log: {self.log_path}")
      self._pump(0.2)
    return self.rx

  def drain(self, seconds=0.5):
    """Collects UART output for a fixed time; returns the new text. Use it to
    assert that something was NOT printed."""
    self.rx = ''
    end = time.time() + seconds
    while time.time() < end:
      self._pump(0.1)
    return self.rx

  # ------------------------------------------------------------- helpers
  def home(self, axis):
    """Full HOME sequence for one axis: wait for the axis to start driving
    toward MIN, press MIN, release it for the backoff, wait for HOMED."""
    self.uart(f'HOME {axis}')
    self.expect(f'A{axis} HOME', timeout=5.0)
    time.sleep(0.2)
    self.press((axis, 'min'))
    self.expect(f'A{axis} MIN HIT', timeout=T_HOME)
    self.release((axis, 'min'))
    self.expect(f'A{axis} HOMED', timeout=T_HOME)

  def fault(self):
    """Latches a fault: hold A1's MIN pressed, then HOME 1 -> MIN ACTIVE.
    Leaves the switch released afterwards."""
    self.press((1, 'min'))
    time.sleep(0.2)          # let the 5 ms ISR debounce see it
    self.uart('HOME 1')
    self.expect('FAULT: A1 MIN ACTIVE', timeout=10.0)
    self.release((1, 'min'))
