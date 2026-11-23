# Stage 1: Baseline Audit & Pinout Migration Analysis

## 1. Branch & Repository Information
- **Dedicated Branch**: `feat/lqfp64-pcb-pinout-untangle`
- **Base Commit**: `c5955d5d558651ae9f518c3ef1c2dcbcccb93229` (off `main`)
- **Status**: Clean working tree; strict adherence to never committing to `main`.

## 2. Baseline Build & Test Status
- **Host Tests (`host_tests/run.sh`)**:
  - Result: **PASS** (14/14 test cases, 113 assertions).
  - Validates homing, step counting, travel measurements, debounce logic, command parsing, and safety interlocks.
- **CMake Presets (`CMakePresets.json`)**:
  - `Debug`: Configures and builds `AA_STM.elf` cleanly (RAM: 2.11%, FLASH: 9.03%).
  - `Release`: Configures and builds cleanly with `-Os`.
  - `Simulation`: Builds cleanly (`AA_STM.elf`, RAM: 2.10%, FLASH: 8.99%).
- **Renode Simulation Suite (`simulation/tests/`)**:
  - Result: 18 passed, 1 failed (19 tests).
  - Failure note: `motion::unexpected_max_is_a_fault` encounters a known race condition on `main` where `update_axis()` in `Core/Src/alignment.c` evaluates debounced active flags while `stop` is momentarily `STOP_NONE`, emitting `FAULT: A1 SWITCH ACTIVE` rather than `FAULT: A1 MAX HIT`. This is resolved in commit `bf3f13a` by latching state snapshots prior to evaluating stop causes.

## 3. KiCad & Physical PCB Topology (*ADA_5)
No `.kicad_pcb` or `.kicad_sch` files are stored in the repo; analysis is derived directly from the KiCad PCB Editor and STM32CubeMX screenshots.

### Connector Layout
1. **Left (West) Side**:
   - `J1`: A1 Stepper Driver (4 pins: STEP, DIR, EN, GND)
   - `J2`: A2 Stepper Driver (4 pins: STEP, DIR, EN, GND)
   - `J3`: A3 Stepper Driver (4 pins: STEP, DIR, EN, GND)
   - `J4`: A4 Stepper Driver (4 pins: STEP, DIR, EN, GND)
   - `J8`: ALFA Driver / Expansion Header (4 pins)
   - `J6`: Limit Switch Supply (2 pins: VCC, GND)
   - `J9`: External Power Supply (2 pins: VIN, GND)
2. **Right (East) Side**:
   - `J5`: Limit Switch Input (8-pin vertical header: A1_MIN, A1_MAX, A2_MIN, A2_MAX, A3_MIN, A3_MAX, A4_MIN, A4_MAX)
3. **Top (North) Side**:
   - `J7`: L298N Dual H-Bridge Header (6 pins: GRP_IN1..4, plus relay/enable)
   - `J11`: ST-LINK SWD Interface (5 pins: 3V3, SWDIO, SWCLK, GND, NRST)
   - `J10`: STM_Interface UART (3 pins: TX, RX, GND)
4. **Bottom (South) Side**:
   - Reserved for decoupling capacitors, bulk filtering, and clear routing channels.

## 4. Root Cause of Existing Ratsnest Tangling
In the prior branch (`pcb-f446ret6`, commit `3443b89`):
- The author mistakenly designated package pins 20–27 (South edge: PA4–PA7, PC4–PC5, PB0–PB1) as "right edge" in commit documentation and assigned all 8 limit switches there.
- Concurrently, Stepper Drivers A1–A3 were assigned to East edge pins 37–45 (PC6–PC9, PA8–PA12) and A4 to North edge pins 51–53.
- Consequently, driver signals assigned on the East/North edges must bridge across the entire MCU core to reach connectors J1–J4 on the West side, while limit switch signals assigned on the South edge cross diagonally over to connector J5 on the East side, resulting in extensive track congestion and 100% ratsnest crossing.
