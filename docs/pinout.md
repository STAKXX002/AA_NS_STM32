# AA pin map (DRAFT - for the STM PCB redesign)

Single source of truth in firmware: the `hw[]` table at the top of `Core/Src/axis.c`.
`axis_init()` configures every axis pin itself, so the CubeMX `.ioc` only needs to carry the
labels and boot levels (see README / CubeMX notes). Pins verified against UM1724 and the
LQFP64 package of the Nucleo-F446RE (note: PB11 does not exist on this package).

20 signals for 4 axes, grouped so each axis lands on one Nucleo connector:

| Axis | Connector | STEP | DIR | EN (active low) | MIN | MAX |
|------|-----------|------|-----|-----------------|-----|-----|
| A1 | CN8 analog A0..A4        | PA0 (A0) | PA1 (A1) | PA4 (A2)  | PB0 (A3)  | PC1 (A4)  |
| A2 | CN5 digital D8..D12      | PA9 (D8) | PC7 (D9) | PB6 (D10) | PA7 (D11) | PA6 (D12) |
| A3 | CN9 digital D2, D4..D7   | PA10 (D2)| PB5 (D4) | PB4 (D5)  | PB10 (D6) | PA8 (D7)  |
| A4 | CN10 morpho, pins 1-6    | PC6 (pin 4) | PC5 (pin 6) | PC8 (pin 2) | PC9 (pin 1) | PB8 (pin 3) |

A3 skips D3 (PB3 = SWO). A4 leaves CN10 pin 5 (PB9) spare.
Still free: PC0 (A5), PB9, PC2, PC3, PB1, PB2.

Unchanged from the Swapping firmware: light relay PB7 (active low, CN7 pin 21), hatch H-bridge
PB12-PB15 (CN10), console USART2 PA2/PA3, SWD PA13/PA14. The fan relay is gone.

Do not use: PA2/PA3, PA13/PA14, PB3, PC13 (button), PA5 (LED), PH0/PH1, PC14/PC15.

Wiring notes
- Power the red endstop boards from 3V3 (CN6), not 5V, so the signal can never exceed 3.3 V.
- GND for the endstop boards and driver signal grounds: CN6 has two GND pins next to 3V3/5V.
- Endstop polarity: `ENDSTOP_ACTIVE_LOW` in `Core/Inc/aa_config.h` (default: pressed = pin low).
- A4/A5 on CN8 are PC1/PC0 in the default solder-bridge configuration; if your board was
  modified (SB for PB9/PB8) A1 MAX would not be on PC1 - check with a multimeter.
