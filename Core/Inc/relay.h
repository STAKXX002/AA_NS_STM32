#ifndef RELAY_H
#define RELAY_H

/* Call once at startup, after MX_GPIO_Init(). Forces the light relay off
 * regardless of whatever level CubeMX's GPIO init left the pin at -
 * don't rely on the .ioc's "GPIO output level" setting alone.
 * (The fan relay was dropped for AA; its pin PB9 is now axis A2 MAX.) */
void relay_init(void);

void light_on(void);
void light_off(void);

#endif /* RELAY_H */
