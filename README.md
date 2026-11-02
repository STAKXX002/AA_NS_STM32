# AA_STM - Active Alignment firmware (STM32F446RE Nucleo)

Four independent lead-screw actuators, each with a STEP/DIR/EN driver and MIN/MAX endstops.
Forked from `AA_NS_STM_PORT` (Swapping system): the hatch and light relay modules are kept,
the fan relay is dropped, the two-axis Z alignment logic is replaced by per-axis control.

## Layout

| Path | Role |
|------|------|
| `Core/Src/axis.c` | TIM3 100 us ISR: step pulses, ramp, **endstop sampling + immediate halt**; pin table |
| `Core/Src/alignment.c` | per-axis state machine: HOME, CAL, MOVE, HALT |
| `Core/Src/commands.c` | UART command parser (115200 8N1 on the ST-Link VCP) |
| `Core/Src/system.c` | global fault latch; any fault stops **all** axes |
| `Core/Inc/aa_config.h` | all tunables (steps/mm, travel, timeouts, endstop polarity) |
| `host_tests/` | PC-side logic tests of the real firmware modules (`./host_tests/run.sh`, needs only gcc) |
| `simulation/` | Renode + pytest (needs the Simulation build) |

## Commands (axes are numbered 1-4)

| Command | Effect |
|---------|--------|
| `HOME <n\|ALL>` | drive to MIN, back off 5 mm, set zero. `ALL` starts every axis at once |
| `CAL <n\|ALL>` | HOME, then drive to MAX and report usable travel (`A1 CAL OK TRAVEL x.xxx`) |
| `MOVE <n> <mm>` | absolute move from zero; needs HOME; rejected with `RANGE` outside 0..travel |
| `HALT <n>` | stop one axis, keep its position |
| `STOP` | stop everything (all axes + hatch), un-home, drivers off; not a fault |
| `POS` | state, position, travel, and raw MIN/MAX state of every axis |
| `RST` | clear a fault, stop everything, re-enable drivers, un-home |
| `OPEN` / `CLOSE` | hatch; refused (`BUSY`) while any axis is moving |
| `ON` / `OFF` | light relay |

An endstop hit during `MOVE`, an unreachable endstop, a timeout, or a switch that is already
pressed at the start of `HOME` latches a fault: every driver is disabled until `RST`.

## First bring-up checklist (single motor, Monday test)

1. In `aa_config.h` set `STEPS_PER_MM` from the screw pitch and driver microstepping (200 is carried over).
2. Confirm endstop polarity: `POS` should show `MIN 0 MAX 0` with both switches released and `1` when pressed.
3. If the carriage moves the wrong way, set `dir_invert` for that axis in `axis.c`.
4. `CAL 1` with the beam well clear of both ends, then put the reported travel into `AXIS_TRAVEL_MM`.
5. Check the NEMA 23 driver's current limit before running under load.

## Build / test

    cmake --preset Release && cmake --build --preset Release      # flash build (watchdog on)
    cmake --preset Simulation && cmake --build --preset Simulation
    ./host_tests/run.sh                                           # fast logic tests, no hardware
    cd simulation && pytest                                       # Renode tests

Known limits: no deceleration ramp (matches the old firmware; moves stop abruptly at the target
or switch); homing repeatability is about one 5 ms debounce of travel (about 0.025 mm at 5 mm/s);
speed is fixed at `STEP_INTERVAL_CRUISE`.
