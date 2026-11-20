# AA pin map - bare STM32F446RET6 (LQFP64) PCB

Single source of truth in firmware: the `hw[]` table at the top of `Core/Src/axis.c`.
`axis_init()` configures every axis pin itself, so the CubeMX `.ioc` only needs to carry the
labels and boot levels. Package pin numbers count counterclockwise from the pin 1 dot
(VBAT): bottom edge 1-16, right edge 17-32, top edge 33-48, left edge 49-64.
Rule used everywhere: connector pin 1 = the group's lowest package pin number.

## Endstops: one 8-pin connector, right edge pins 20-27

| Conn pin | Pkg pin | MCU pin | Signal |
|----------|---------|---------|--------|
| 1 | 20 | PA4 | A1 MIN |
| 2 | 21 | PA5 | A1 MAX |
| 3 | 22 | PA6 | A2 MIN |
| 4 | 23 | PA7 | A2 MAX |
| 5 | 24 | PC4 | A3 MIN |
| 6 | 25 | PC5 | A3 MAX |
| 7 | 26 | PB0 | A4 MIN |
| 8 | 27 | PB1 | A4 MAX |

Signals only; each endstop board also needs 3V3 and GND (route separately, or use a 10-pin
connector). PB2 (pkg 28) is BOOT1 and is deliberately not used: a released switch pulls it high,
and BOOT0 high with BOOT1 high boots SRAM instead of the system bootloader.

## Drivers: STEP / DIR / EN (active low), three adjacent pins per axis

| Axis | Edge | Pkg pins | STEP | DIR | EN |
|------|------|----------|------|-----|----|
| A1 | top  | 37-39 | PC6  | PC7  | PC8  |
| A2 | top  | 40-42 | PC9  | PA8  | PA9  |
| A3 | top  | 43-45 | PA10 | PA11 | PA12 |
| A4 | left | 51-53 | PC10 | PC11 | PC12 |

## Unchanged

Light relay PB7 (pkg 59, active low), hatch H-bridge PB12-PB15 (pkg 33-36), console USART2
PA2/PA3 (pkg 16/17), SWD PA13/PA14 (pkg 46/49). Spare: PB10, PB3 (SWO), PB4-PB6, PB8, PB9,
PA15, PC0-PC3, PC13, PD2. Do not use PB2 (BOOT1), PH0/PH1 (crystal), PC14/PC15.

## What the Nucleo did that the PCB must now provide

- HSE: the Nucleo fed an 8 MHz clock from the ST-LINK, so the firmware used HSE bypass. The PCB
  needs a crystal on PH0/PH1 with load caps, and `RCC_HSE_ON` instead of `RCC_HSE_BYPASS`.
- Console: no ST-LINK virtual COM port. Break out PA2/PA3 + GND to a header for a 3.3 V USB-UART.
- Programming: SWD header (PA13, PA14, NRST, 3V3, GND; PB3 for SWO if wanted).
- BOOT0 (pkg 60): 10 kohm pull-down, with a jumper to 3V3 for the ROM bootloader.
- NRST: 100 nF to GND. VCAP_1 (pkg 30): 2.2 uF low-ESR to GND. 100 nF on every VDD/VDDA,
  plus the bulk and VBAT connection per the datasheet reference design.
- LED/button: LD1 (PA5) and B1 (PC13) no longer exist. PA5 is an endstop input, so no code may
  configure it as an output.
- EN boot level: all GPIO float until firmware runs. Add a pull on each EN line to whichever
  level disables your driver, and make the CubeMX initial level of the EN pins High.
