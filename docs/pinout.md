# STM32F446RET6 (LQFP64) Pinout Specification (*ADA_5 PCB)

## 1. Overview & PCB Physical Architecture
This document specifies the routing-optimized pinout for the standalone **STM32F446RET6** (LQFP64 package) on the custom **`*ADA_5`** board.

The pin assignment is strictly oriented to match the physical placement of peripheral connectors around the MCU on `*ADA_5`, completely untangling ratsnest crossings, isolating high di/dt motor switching noise from analog and reset circuits, and simplifying single- and two-layer PCB routing.

```
                         [J7: L298N Hatch] [J11: ST-LINK] [J10: UART]
                                  NORTH EDGE (Pins 49–64)
                        +-----------------------------------------+
    [J1: A1 Driver]     | 64                                   49 |
    [J2: A2 Driver] WEST|                                         |EAST  [J5: Limit Switch Input]
    [J3: A3 Driver] EDGE|              STM32F446RET6              |EDGE  (8-pin vertical header)
    [J4: A4 Driver]     |                 (LQFP64)                |
    [J8: ALFA Driver]   | 1                                    33 |
                        +-----------------------------------------+
                                  SOUTH EDGE (Pins 17–32)
                                 [Power / Decoupling / Spare]
```

---

## 2. Per-Side LQFP64 Pin Tables

### West Edge (Pins 1–16) — Faces Stepper Drivers J1 & J2, Crystal, and Analog Filter
| Pin # | Pin Name | Signal / Net Name | Target Connector | Type | 5V Tol | Function / Description |
|:-----:|:---------|:------------------|:-----------------|:----:|:------:|:-----------------------|
| 1 | VBAT | +3V3_VBAT | Power | Power | - | Battery backup supply (tied to 3V3 via filter) |
| 2 | PC13 | SPARE_PC13 | Internal / Test | I/O | FT | Spare general purpose I/O |
| 3 | PC14 | PC14_OSC32_IN | Internal | I/O | TC | LSE crystal in / Spare I/O |
| 4 | PC15 | PC15_OSC32_OUT | Internal | I/O | TC | LSE crystal out / Spare I/O |
| 5 | PH0 | RCC_OSC_IN | Y1 (8 MHz Crystal) | In | TC | HSE crystal input (isolated crystal loop) |
| 6 | PH1 | RCC_OSC_OUT | Y1 (8 MHz Crystal) | Out | TC | HSE crystal output (isolated crystal loop) |
| 7 | NRST | MCU_NRST | J11 (ST-LINK) / SW1 | In | - | Active-low system reset (100nF to GND) |
| 8 | PC0 | `A2_STEP` | J2 (Pin 1) | Out | FT | Axis A2 step pulse (active high) |
| 9 | PC1 | `A2_DIR` | J2 (Pin 2) | Out | FT | Axis A2 direction |
| 10 | PC2 | `A2_EN` | J2 (Pin 3) | Out | FT | Axis A2 driver enable (active low, boot high) |
| 11 | PC3 | SPARE_PC3 | Internal / Test | I/O | FT | Spare general purpose I/O |
| 12 | VSSA | AGND | Power | Power | - | Analog ground plane connection |
| 13 | VDDA | +3V3A | Power | Power | - | Filtered analog 3.3V (ferrite bead + 1µF/10nF) |
| 14 | PA0 | SPARE_PA0 | Internal / Test | I/O | FT | Spare general purpose I/O (WKUP) |
| 15 | PA1 | SPARE_PA1 | Internal / Test | I/O | FT | Spare general purpose I/O |
| 16 | PA2 | `USART2_TX` | J10 (Pin 1) | Out | FT | Console serial TX (AF07) |

---

### South Edge (Pins 17–32) — Faces Stepper Drivers J3, J4, J8 (ALFA), and Decoupling
| Pin # | Pin Name | Signal / Net Name | Target Connector | Type | 5V Tol | Function / Description |
|:-----:|:---------|:------------------|:-----------------|:----:|:------:|:-----------------------|
| 17 | PA3 | `USART2_RX` | J10 (Pin 2) | In | FT | Console serial RX (AF07) |
| 18 | VSS | GND | Power | Power | - | Core ground |
| 19 | VDD | +3V3 | Power | Power | - | Digital 3.3V supply (100nF bypass) |
| 20 | PA4 | `A3_STEP` | J3 (Pin 1) | Out | FT | Axis A3 step pulse (active high) |
| 21 | PA5 | `A3_DIR` | J3 (Pin 2) | Out | FT | Axis A3 direction |
| 22 | PA6 | `A3_EN` | J3 (Pin 3) | Out | FT | Axis A3 driver enable (active low, boot high) |
| 23 | PA7 | `A4_STEP` | J4 (Pin 1) | Out | FT | Axis A4 step pulse (active high) |
| 24 | PC4 | `A4_DIR` | J4 (Pin 2) | Out | FT | Axis A4 direction |
| 25 | PC5 | `A4_EN` | J4 (Pin 3) | Out | FT | Axis A4 driver enable (active low, boot high) |
| 26 | PB0 | `ALFA_STEP` | J8 (Pin 1) | Out | FT | ALFA driver step / Spare I/O |
| 27 | PB1 | `ALFA_DIR` | J8 (Pin 2) | Out | FT | ALFA driver dir / Spare I/O |
| 28 | PB2 | BOOT1 | Internal | In | FT | Boot configuration pin (pull-down 10k to GND) |
| 29 | PB10 | `ALFA_EN` | J8 (Pin 3) | Out | FT | ALFA driver enable / Spare I/O |
| 30 | VCAP_1 | VCAP_1 | C_VCAP | Power | - | Internal regulator bypass (4.7µF ceramic to GND) |
| 31 | VSS | GND | Power | Power | - | Core ground |
| 32 | VDD | +3V3 | Power | Power | - | Digital 3.3V supply (100nF bypass) |

---

### East Edge (Pins 33–48) — Faces Connector J5 (Limit Switch Inputs)
| Pin # | Pin Name | Signal / Net Name | Target Connector | Type | 5V Tol | Function / Description |
|:-----:|:---------|:------------------|:-----------------|:----:|:------:|:-----------------------|
| 33 | PB12 | `A1_MIN` | J5 (Pin 1) | In | FT | Axis A1 minimum limit switch (Active low, pull-up) |
| 34 | PB13 | `A1_MAX` | J5 (Pin 2) | In | FT | Axis A1 maximum limit switch (Active low, pull-up) |
| 35 | PB14 | `A2_MIN` | J5 (Pin 3) | In | FT | Axis A2 minimum limit switch (Active low, pull-up) |
| 36 | PB15 | `A2_MAX` | J5 (Pin 4) | In | FT | Axis A2 maximum limit switch (Active low, pull-up) |
| 37 | PC6 | `A3_MIN` | J5 (Pin 5) | In | FT | Axis A3 minimum limit switch (Active low, pull-up) |
| 38 | PC7 | `A3_MAX` | J5 (Pin 6) | In | FT | Axis A3 maximum limit switch (Active low, pull-up) |
| 39 | PC8 | `A4_MIN` | J5 (Pin 7) | In | FT | Axis A4 minimum limit switch (Active low, pull-up) |
| 40 | PC9 | `A4_MAX` | J5 (Pin 8) | In | FT | Axis A4 maximum limit switch (Active low, pull-up) |
| 41 | PA8 | SPARE_PA8 | Internal / Test | I/O | FT | Spare general purpose I/O |
| 42 | PA9 | SPARE_PA9 | Internal / Test | I/O | FT | Spare general purpose I/O (USART1_TX capable) |
| 43 | PA10 | SPARE_PA10 | Internal / Test | I/O | FT | Spare general purpose I/O (USART1_RX capable) |
| 44 | PA11 | SPARE_PA11 | Internal / Test | I/O | FT | Spare general purpose I/O |
| 45 | PA12 | SPARE_PA12 | Internal / Test | I/O | FT | Spare general purpose I/O |
| 46 | PA13 | `SYS_JTMS-SWDIO` | J11 (Pin 2) | I/O | FT | SWD data line (AF00) |
| 47 | VSS | GND | Power | Power | - | Core ground |
| 48 | VDD | +3V3 | Power | Power | - | Digital 3.3V supply (100nF bypass) |

---

### North Edge (Pins 49–64) — Faces J7 (L298N Hatch/Relay), J11 (ST-LINK), and J1 (A1 Driver)
| Pin # | Pin Name | Signal / Net Name | Target Connector | Type | 5V Tol | Function / Description |
|:-----:|:---------|:------------------|:-----------------|:----:|:------:|:-----------------------|
| 49 | PA14 | `SYS_JTCK-SWCLK` | J11 (Pin 3) | In | FT | SWD clock line (AF00) |
| 50 | PA15 | SPARE_PA15 | Internal / Test | I/O | FT | Spare general purpose I/O (JTDI) |
| 51 | PC10 | `A1_STEP` | J1 (Pin 1) | Out | FT | Axis A1 step pulse (active high) |
| 52 | PC11 | `A1_DIR` | J1 (Pin 2) | Out | FT | Axis A1 direction |
| 53 | PC12 | `A1_EN` | J1 (Pin 3) | Out | FT | Axis A1 driver enable (active low, boot high) |
| 54 | PD2 | SPARE_PD2 | Internal / Test | I/O | FT | Spare general purpose I/O |
| 55 | PB3 | SPARE_PB3 | J11 (Pin 5) | I/O | FT | SWO / JTDO (trace debug) / Spare I/O |
| 56 | PB4 | `GRIP_IN1` | J7 (Pin 1) | Out | FT | Hatch H-bridge IN1 (push actuator) |
| 57 | PB5 | `GRIP_IN2` | J7 (Pin 2) | Out | FT | Hatch H-bridge IN2 (retract actuator) |
| 58 | PB6 | `GRIP_IN3` | J7 (Pin 3) | Out | FT | Hatch H-bridge IN3 |
| 59 | PB7 | `GRIP_IN4` | J7 (Pin 4) | Out | FT | Hatch H-bridge IN4 |
| 60 | BOOT0 | BOOT0 | Internal | In | - | Boot configuration pin (10k pull-down to GND) |
| 61 | PB8 | `RELAY_LIGHT` | J7 (Pin 5) / K1 | Out | FT | Illumination relay control (Active low, boot high) |
| 62 | PB9 | `L298N_EN` | J7 (Pin 6) | Out | FT | Hatch driver enable / Spare output |
| 63 | VSS | GND | Power | Power | - | Core ground |
| 64 | VDD | +3V3 | Power | Power | - | Digital 3.3V supply (100nF bypass) |

---

## 3. PCB Connector Mapping (*ADA_5)

### J1: Axis A1 Driver Header (4 pins, Top-West)
| Pin # | Signal Name | MCU Pin | MCU Ball/Pad | Notes |
|:-----:|:------------|:-------:|:------------:|:------|
| 1 | `A1_STEP` | PC10 | Pin 51 | Output (North-West corner) |
| 2 | `A1_DIR`  | PC11 | Pin 52 | Output |
| 3 | `A1_EN`   | PC12 | Pin 53 | Output (Active low, initial high) |
| 4 | GND       | GND  | Plane        | Ground return |

### J2: Axis A2 Driver Header (4 pins, Upper-West)
| Pin # | Signal Name | MCU Pin | MCU Ball/Pad | Notes |
|:-----:|:------------|:-------:|:------------:|:------|
| 1 | `A2_STEP` | PC0  | Pin 8  | Output (West edge) |
| 2 | `A2_DIR`  | PC1  | Pin 9  | Output |
| 3 | `A2_EN`   | PC2  | Pin 10 | Output (Active low, initial high) |
| 4 | GND       | GND  | Plane  | Ground return |

### J3: Axis A3 Driver Header (4 pins, Lower-West)
| Pin # | Signal Name | MCU Pin | MCU Ball/Pad | Notes |
|:-----:|:------------|:-------:|:------------:|:------|
| 1 | `A3_STEP` | PA4  | Pin 20 | Output (South-West edge) |
| 2 | `A3_DIR`  | PA5  | Pin 21 | Output |
| 3 | `A3_EN`   | PA6  | Pin 22 | Output (Active low, initial high) |
| 4 | GND       | GND  | Plane  | Ground return |

### J4: Axis A4 Driver Header (4 pins, Bottom-West)
| Pin # | Signal Name | MCU Pin | MCU Ball/Pad | Notes |
|:-----:|:------------|:-------:|:------------:|:------|
| 1 | `A4_STEP` | PA7  | Pin 23 | Output (South-West edge) |
| 2 | `A4_DIR`  | PC4  | Pin 24 | Output |
| 3 | `A4_EN`   | PC5  | Pin 25 | Output (Active low, initial high) |
| 4 | GND       | GND  | Plane  | Ground return |

### J5: Limit Switch Input Header (8 pins, East)
| Pin # | Signal Name | MCU Pin | MCU Ball/Pad | EXTI Line | 5V Tol |
|:-----:|:------------|:-------:|:------------:|:---------:|:------:|
| 1 | `A1_MIN` | PB12 | Pin 33 | EXTI12 | FT |
| 2 | `A1_MAX` | PB13 | Pin 34 | EXTI13 | FT |
| 3 | `A2_MIN` | PB14 | Pin 35 | EXTI14 | FT |
| 4 | `A2_MAX` | PB15 | Pin 36 | EXTI15 | FT |
| 5 | `A3_MIN` | PC6  | Pin 37 | EXTI6  | FT |
| 6 | `A3_MAX` | PC7  | Pin 38 | EXTI7  | FT |
| 7 | `A4_MIN` | PC8  | Pin 39 | EXTI8  | FT |
| 8 | `A4_MAX` | PC9  | Pin 40 | EXTI9  | FT |

### J7: L298N Hatch / Gripper & Relay Header (6 pins, North)
| Pin # | Signal Name | MCU Pin | MCU Ball/Pad | Function |
|:-----:|:------------|:-------:|:------------:|:---------|
| 1 | `GRIP_IN1`    | PB4 | Pin 56 | H-Bridge Push Actuator |
| 2 | `GRIP_IN2`    | PB5 | Pin 57 | H-Bridge Retract Actuator |
| 3 | `GRIP_IN3`    | PB6 | Pin 58 | H-Bridge Polar Control |
| 4 | `GRIP_IN4`    | PB7 | Pin 59 | H-Bridge Polar Control |
| 5 | `RELAY_LIGHT` | PB8 | Pin 61 | Active-Low Light Relay |
| 6 | `L298N_EN`    | PB9 | Pin 62 | Driver Enable / PWM |

### J10: STM_Interface UART Header (3 pins, North-East)
| Pin # | Signal Name | MCU Pin | MCU Ball/Pad | Function |
|:-----:|:------------|:-------:|:------------:|:---------|
| 1 | `USART2_TX` | PA2 | Pin 16 | Serial Transmit (AF07) |
| 2 | `USART2_RX` | PA3 | Pin 17 | Serial Receive (AF07) |
| 3 | GND         | GND | Plane  | Ground |

### J11: ST-LINK SWD Debug Header (5 pins, North)
| Pin # | Signal Name | MCU Pin | MCU Ball/Pad | Function |
|:-----:|:------------|:-------:|:------------:|:---------|
| 1 | +3V3        | VDD  | Power  | Target Voltage Reference |
| 2 | `SWDIO`     | PA13 | Pin 46 | Serial Wire Data (AF00) |
| 3 | `SWCLK`     | PA14 | Pin 49 | Serial Wire Clock (AF00) |
| 4 | GND         | GND  | Plane  | Ground |
| 5 | `NRST`      | NRST | Pin 7  | Hardware Reset |

### Auxiliary / Expansion Headers
- **`J8` (ALFA Driver / Spare, 4 pins, Bottom-West)**:
  Pin 1: `ALFA_STEP` (PB0, Pin 26), Pin 2: `ALFA_DIR` (PB1, Pin 27), Pin 3: `ALFA_EN` (PB10, Pin 29), Pin 4: GND.
- **`J6` (Limit Switch Supply, 2 pins, Bottom-West)**:
  Pin 1: +3V3, Pin 2: GND.
- **`J9` (External Power Input, 2 pins, Bottom-West)**:
  Pin 1: VIN (+12V/+24V), Pin 2: GND.

---

## 4. Hardware Verification & Design Rules

### 4.1 EXTI Line Collision Check
On STM32F4, EXTI lines 0 through 15 are multiplexed across Ports A through H. Multiple pins sharing the same pin index cannot trigger independent EXTI interrupts simultaneously.

All 8 limit switches on connector J5 are mapped to distinct pin indices:
- `A1_MIN`: PB12 $\rightarrow$ EXTI Line 12
- `A1_MAX`: PB13 $\rightarrow$ EXTI Line 13
- `A2_MIN`: PB14 $\rightarrow$ EXTI Line 14
- `A2_MAX`: PB15 $\rightarrow$ EXTI Line 15
- `A3_MIN`: PC6  $\rightarrow$ EXTI Line 6
- `A3_MAX`: PC7  $\rightarrow$ EXTI Line 7
- `A4_MIN`: PC8  $\rightarrow$ EXTI Line 8
- `A4_MAX`: PC9  $\rightarrow$ EXTI Line 9

**Result**: **Zero EXTI line conflicts**. Every limit switch has a dedicated EXTI line.

### 4.2 Alternate Function (AF) & Peripheral Mapping
- **USART2**: PA2 (AF07, TX) and PA3 (AF07, RX). Compatible with console host tools and Renode simulation platform.
- **SWD Debug**: PA13 (AF00, SWDIO) and PA14 (AF00, SWCLK). Dedicated debug lines facing J11.
- **HSE Oscillator**: PH0 (OSC_IN) and PH1 (OSC_OUT). Driven by an external 8.0 MHz crystal with two 18 pF load capacitors.
- **TIM3**: Configured as internal periodic tick timer (100 µs interval, prescaler 89, period 99). Internal clock source (`TIM3_VS_ClockSourceINT`); no external pin required.
- **IWDG**: Internal low-speed oscillator (LSI 32 kHz); no external pin required.

### 4.3 5V-Tolerance (FT)
- All limit switch inputs (`PB12..PB15`, `PC6..PC9`) are **FT (5V-tolerant)**. If 5V optical or mechanical endstops are connected, pins are protected against over-voltage damage up to VDD + 4.0V.
- All motor driver outputs (`PC10..PC12`, `PC0..PC2`, `PA4..PA6`, `PA7`, `PC4..PC5`) are **FT**.
- All L298N hatch control outputs (`PB4..PB8`) are **FT**.

### 4.4 Noise Immunity & Sensitive Pin Routing Guidelines
- **Analog Isolation**: `VDDA` (Pin 13) and `VSSA` (Pin 12) are separated from stepper outputs. Decoupling capacitors (100 nF ceramic + 1 µF ceramic/tantalum) are located immediately adjacent to Pin 12 and 13.
- **HSE Crystal Isolation**: The 8 MHz crystal loop (`PH0`/`PH1`) is guarded by a ground ring, with load capacitors placed within 5 mm of pins 5 and 6. No motor pulse traces cross under or near the crystal loop.
- **Reset Line (`NRST`)**: Pin 7 is decoupled with a 100 nF capacitor to digital ground placed close to the pin to prevent spurious resets caused by inductive motor kickback.
- **Regulator Bypass (`VCAP_1`)**: Pin 30 requires a 4.7 µF low-ESR ceramic capacitor connected directly to GND.
