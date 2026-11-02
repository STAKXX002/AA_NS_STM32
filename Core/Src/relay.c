#include "relay.h"
#include "main.h"    /* RELAY_LIGHT_* pin macros from CubeMX */
#include "system.h"  /* system_in_fault() */

void relay_init(void) {
    light_off();
}

/* Defense in depth: even if a caller forgets to check, nothing can
 * energize a relay while the system is faulted. */
void light_on(void) {
    if (system_in_fault()) return;
    HAL_GPIO_WritePin(RELAY_LIGHT_GPIO_Port, RELAY_LIGHT_Pin, GPIO_PIN_RESET); /* Active-LOW */
}

void light_off(void) {
    HAL_GPIO_WritePin(RELAY_LIGHT_GPIO_Port, RELAY_LIGHT_Pin, GPIO_PIN_SET);   /* Active-LOW */
}
