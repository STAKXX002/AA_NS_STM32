# AA pin map (DRAFT - for the STM PCB redesign)

Single source of truth in firmware: the `hw[]` table at the top of `Core/Src/axis.c`.
Change a pin there and nothing else needs to know. The CubeMX `.ioc` still carries the
old Swapping-system labels for PB0-PB2, PB4-PB6, PB8-PB10; `axis_init()` reconfigures
those pins itself, so the `.ioc` is not authoritative for them.

20 signals for 4 axes: 3 outputs (STEP/DIR/EN) + 2 inputs (MIN/MAX) per axis.

| Axis | STEP | DIR | EN (active low) | MIN | MAX |
|------|------|-----|-----------------|-----|-----|
| A1   | PB0  | PB1 | PB2             | PB10| PB11|
| A2   | PB4  | PB5 | PB6             | PB8 | PB9 |
| A3   | PC0  | PC1 | PC2             | PC3 | PC4 |
| A4   | PC5  | PC6 | PC8             | PC9 | PC10|

Unchanged from the Swapping firmware: light relay PB7 (active low), hatch H-bridge PB12-PB15,
console USART2 PA2/PA3, SWD PA13/PA14. The fan relay is gone (PB9 is now A2 MAX).

Avoid for anything new: PA2/PA3, PA13/PA14, PB3 (SWO), PC13 (user button), PA5 (LED),
PH0/PH1 (HSE bypass), PC14/PC15.

Endstops: polarity set by `ENDSTOP_ACTIVE_LOW` in `Core/Inc/aa_config.h`
(default: switch pulls the pin low, internal pull-up). Verify against the red endstop boards.
