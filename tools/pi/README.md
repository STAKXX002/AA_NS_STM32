# Raspberry Pi 4B $\longleftrightarrow$ STM32F446 UART5 Serial Verification & Bench Checklist

This directory contains the Python verification utility (`serial_test.py`) for testing the bidirectional serial link between the **Raspberry Pi 4B** and the **STM32F446RET6** running on **`UART5`** (`PC12` / `PD2`).

---

## 1. Physical Wiring & Pin Mapping

Connect the Raspberry Pi 4B 40-pin GPIO header to the STM32 Nucleo-64 (or custom PCB connector `J10`) using female-to-female jumper wires as follows:

| STM32 Signal | STM32 Nucleo Pin | PCB Connector J10 | Raspberry Pi 4B Physical Header | Pi Function / BCM |
|:---|:---:|:---:|:---:|:---|
| **`UART5_TX`** (MCU Out) | **`PC12`** (CN7 Pin 3) | **Pin 2** (`PI_UART5_TX`) | **Physical Pin 10** | `GPIO15` (`RXD0` / Serial In) |
| **`UART5_RX`** (MCU In)  | **`PD2`**  (CN7 Pin 4) | **Pin 3** (`PI_UART5_RX`) | **Physical Pin 8**  | `GPIO14` (`TXD0` / Serial Out) |
| **`GND`** (Common Ref)   | **`GND`**  (CN7 Pin 8) | **Pin 1** (`Logic GND`)   | **Physical Pin 6**  | Ground (`GND`) |

> [!CAUTION]
> **DO NOT CONNECT ANY 5V SUPPLY PIN BETWEEN THE PI AND STM32.**  
> Both the STM32F446 (`VDD` = 3.3V) and Raspberry Pi 4B (3.3V CMOS GPIO) operate at **3.3V logic levels**.  
> Connecting 5V to any Raspberry Pi GPIO pin will **permanently destroy the Raspberry Pi SoC**.

---

## 2. Raspberry Pi OS UART Configuration

### Step A: Disable Login Console & Enable Serial Hardware
1. On the Raspberry Pi, launch the configuration tool:
   ```bash
   sudo raspi-config
   ```
2. Navigate to: **`Interface Options`** $\rightarrow$ **`Serial Port`**.
   - *"Would you like a login shell to be accessible over serial?"* $\longrightarrow$ Select **`<No>`**
   - *"Would you like the serial port hardware to be enabled?"* $\longrightarrow$ Select **`<Yes>`**
3. Select **`<Finish>`** and reboot if prompted.

### Step B: High-Performance Hardware UART (PL011) vs. Mini UART
On the Raspberry Pi 4B, the primary alias `/dev/serial0` points by default to `/dev/ttyS0` (the auxiliary Mini UART) because the high-performance hardware PL011 UART (`/dev/ttyAMA0`) is assigned to the onboard Bluetooth modem. The Mini UART baud rate fluctuates with core clock frequency changes unless stabilized.

For rock-solid serial communication, switch the primary UART to the PL011 controller:
1. Edit the boot configuration file:
   - On **Debian Bookworm (Raspberry Pi OS 12)**:
     ```bash
     sudo nano /boot/firmware/config.txt
     ```
   - On **Debian Bullseye / older releases**:
     ```bash
     sudo nano /boot/config.txt
     ```
2. Add one of the following overlays at the bottom of the file:
   - **Option 1 (Recommended if Bluetooth is not needed):**
     ```ini
     dtoverlay=disable-bt
     ```
     *(Completely frees the PL011 hardware UART for GPIO14/15).*
   - **Option 2 (If Bluetooth is required):**
     ```ini
     dtoverlay=miniuart-bt
     ```
     *(Moves Bluetooth to the Mini UART and assigns PL011 to GPIO14/15).*
3. Also ensure `enable_uart=1` is present in the file.
4. Save and reboot:
   ```bash
   sudo reboot
   ```
5. Verify the configuration:
   ```bash
   ls -l /dev/serial0
   ```
   **Expected output:**
   ```text
   lrwxrwxrwx 1 root root 7 Nov 27 10:00 /dev/serial0 -> ttyAMA0
   ```

---

## 3. Pre-Flight Bench Safety Checklist

Before powering on the system or executing tests:
1. [ ] **Motors Disconnected:** Unplug all stepper motors from drivers `A1`..`A4` or disconnect motor power supply (`VMOT`).
   * *Reason:* On branch `feat/pcb-lqfp64-uart5-integration`, `A1_EN` was shifted from `PC12` to `PA15` (Pin 50). If drivers are powered before verified firmware is running, floating enable lines could activate motor current.
2. [ ] **Hatch & Relays Disconnected:** Disconnect the L298N hatch driver and illumination relay (`J7`) until communication is verified.
3. [ ] **Common Ground Verified:** Measure resistance between STM32 GND and Raspberry Pi GND with a multimeter (must be $< 1\text{ }\Omega$).
4. [ ] **Voltage Levels Confirmed:** Ensure no line exceeds 3.3V DC with respect to ground.

---

## 4. Standalone Raspberry Pi Loopback Self-Test

Before connecting the STM32, verify the Raspberry Pi's own UART hardware and driver stack:
1. Disconnect the STM32 completely.
2. Connect a jumper wire directly between **Pin 8 (GPIO14/TXD)** and **Pin 10 (GPIO15/RXD)** on the Pi.
3. Install dependencies:
   ```bash
   pip3 install -r tools/pi/requirements.txt
   ```
4. Run python to verify loopback transmission:
   ```bash
   python3 -c "
   import serial
   s = serial.Serial('/dev/serial0', 115200, timeout=1)
   s.write(b'HELLO_LOOPBACK\n')
   reply = s.readline()
   print('Received:', reply.decode())
   assert reply == b'HELLO_LOOPBACK\n', 'Loopback failed!'
   print('Loopback test PASSED!')
   "
   ```
5. Remove the jumper wire after the test passes.

---

## 5. Running `serial_test.py`

### Safety Guard Policy
The STM32 firmware has no `PING` or `STATUS` command. All actuator motion and relay control commands are guarded and blocked by default:
- **Unguarded commands (safe by default):** `POS` (read-only query), `STOP` (emergency halt), `HALT` (axis decelerate), `RST` (fault reset).
- **Guarded commands (require `--allow-motion`):** `HOME`, `CAL`, `MOVE`, `OPEN`, `CLOSE`, `ON` (illumination relay), `OFF` (illumination relay).

---

### Mode 1: Single Ping Verification (`ping`)
Sends `POS` (safe read-only query) to the STM32. Success requires the full reply to match four valid axis lines (`A1`..`A4`):
```text
A<n> <STATE> POS <f> TRAVEL <f> MIN <0|1> MAX <0|1>
```
Anything else, including `UNKNOWN`, incomplete lines, or framing/baud garbage, counts as failure and prints the raw received bytes in hex.

```bash
python3 tools/pi/serial_test.py --port /dev/serial0 ping
```
**Expected Output (Success):**
```text
PING OK: 4 axes verified (POS), latency=2.45 ms
  A1 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0
  A2 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0
  A3 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0
  A4 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0
```

**Expected Output (Failure with Hex Dump on Framing / Error):**
```text
PING FAILED: Line 1 invalid format: 'UNKNOWN'
  Raw received (hex): 55 4E 4B 4E 4F 57 4E 0D 0A
  Decoded text: ['UNKNOWN']
```

---

### Mode 2: Parser Check (`probe`)
Sends the unmapped token `PING` to test the STM32 command parser alone. The parser must reply with exactly `UNKNOWN\r\n`.
*(Labelled as a parser check, not a status check).*

```bash
python3 tools/pi/serial_test.py --port /dev/serial0 probe
```
**Expected Output:**
```text
PARSER CHECK OK: MCU command parser replied 'UNKNOWN', latency=2.15 ms
```

---

### Mode 3: System Status Query (`status`)
Sends `POS` and prints a structured tabular report of all axes and switch states:

```bash
python3 tools/pi/serial_test.py --port /dev/serial0 status
```
**Expected Output:**
```text
--- STM32 Axis Status (POS) ---
Axis   State      Pos (mm)     Travel (mm)  Min Sw   Max Sw  
-----------------------------------------------------------
A1     UNHOMED    0.000        0.000        0        0       
A2     UNHOMED    0.000        0.000        0        0       
A3     UNHOMED    0.000        0.000        0        0       
A4     UNHOMED    0.000        0.000        0        0       
```

---

### Mode 4: Round-Trip Latency & Packet Loss Benchmark (`loop [N]`)
Executes $N$ continuous `POS` round-trips with strict 4-line response validation. Dropped frames or corrupt lines are reported with hex dumps:

```bash
python3 tools/pi/serial_test.py --port /dev/serial0 loop 20
```
**Expected Output:**
```text
--- Starting 20 POS round-trips to STM32 (/dev/serial0 @ 115200 baud) ---
[  1/20] OK: 4 axes verified (POS)  latency:   2.35 ms
[  2/20] OK: 4 axes verified (POS)  latency:   2.18 ms
...
[ 20/20] OK: 4 axes verified (POS)  latency:   2.25 ms

--- Ping Round-Trip Statistics (POS) ---
Packets: Sent = 20, Received = 20, Lost = 0 (0.0% loss)
Latency: Min = 2.15 ms, Avg = 2.31 ms, Max = 2.68 ms
```

---

### Mode 5: Interactive Serial Console (`interactive`)
Launches a command-line terminal to send arbitrary commands to the MCU:
```bash
python3 tools/pi/serial_test.py --port /dev/serial0 interactive
```
**Safety Enforcement Example:**
Actuation commands (`HOME`, `CAL`, `MOVE`, `OPEN`, `CLOSE`, `ON`, `OFF`) are blocked by default:
```text
STM32> ON
  [BLOCKED] Command 'ON' blocked by default safety policy (motion/relay command). Pass --allow-motion to enable.
STM32> MOVE A1 10
  [BLOCKED] Command 'MOVE A1 10' blocked by default safety policy (motion/relay command). Pass --allow-motion to enable.
STM32> POS
  < A1 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0
  < A2 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0
  < A3 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0
  < A4 UNHOMED POS 0.000 TRAVEL 0.000 MIN 0 MAX 0
  (round-trip: 2.4 ms | hex: 41 31 ...)
```
To enable motion/relay commands when the bench hardware is connected and ready:
```bash
python3 tools/pi/serial_test.py --port /dev/serial0 --allow-motion interactive
```

---

## 6. Troubleshooting Guide

| Symptom | Probable Cause | Corrective Action |
|:---|:---|:---|
| **No response / Timeout** | TX and RX swapped | Verify crossover: STM32 `PC12` (TX) must connect to Pi Pin 10 (`RXD`), and STM32 `PD2` (RX) must connect to Pi Pin 8 (`TXD`). |
| | Common ground missing | Connect STM32 GND to Pi Pin 6 (`GND`). Without a shared ground reference, floating potentials corrupt UART frames. |
| | Serial port held by Linux login console | Run `sudo raspi-config` and disable serial login shell. Verify `console=serial0` is removed from `/boot/firmware/cmdline.txt`. |
| | Firmware not running on STM32 | Verify STM32 power LED and ST-LINK connection. Flash `build/Debug/AA_STM.elf`. |
| **Garbage / Corrupted characters** | Baud rate mismatch | Ensure `--baud 115200` is used. Check that the STM32 clock tree matches 180 MHz system clock. Raw hex output reveals baud mismatches. |
| | Pi Mini UART frequency drift | Add `dtoverlay=disable-bt` to `/boot/firmware/config.txt` and reboot to lock hardware PL011 UART. |
| **Only one direction working** | One jumper wire loose or damaged | Test jumpers with multimeter continuity check. Run loopback test on Pi (Pin 8 to 10). |
| | MCU pin misconfiguration | Check that `__HAL_RCC_UART5_CLK_ENABLE()`, `GPIOC`, and `GPIOD` clocks are active and alternate function AF8 is set. |
| **`Permission denied: '/dev/serial0'`** | User not in `dialout` group | Run `sudo usermod -a -G dialout $USER`, then log out and log back in. |
