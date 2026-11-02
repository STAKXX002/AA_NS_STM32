/* Host-side stand-in for the CubeMX main.h + HAL, just enough to compile
 * axis.c / alignment.c / commands.c / hatch.c / relay.c / system.c on a PC
 * and drive them from test_aa.c with a fake GPIO and a physical model. */
#ifndef MAIN_H
#define MAIN_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct { volatile uint32_t IDR, ODR; } GPIO_TypeDef;
extern GPIO_TypeDef fakeGPIOA, fakeGPIOB, fakeGPIOC;
#define GPIOA (&fakeGPIOA)
#define GPIOB (&fakeGPIOB)
#define GPIOC (&fakeGPIOC)

#define GPIO_PIN_0  ((uint16_t)0x0001)
#define GPIO_PIN_1  ((uint16_t)0x0002)
#define GPIO_PIN_2  ((uint16_t)0x0004)
#define GPIO_PIN_3  ((uint16_t)0x0008)
#define GPIO_PIN_4  ((uint16_t)0x0010)
#define GPIO_PIN_5  ((uint16_t)0x0020)
#define GPIO_PIN_6  ((uint16_t)0x0040)
#define GPIO_PIN_7  ((uint16_t)0x0080)
#define GPIO_PIN_8  ((uint16_t)0x0100)
#define GPIO_PIN_9  ((uint16_t)0x0200)
#define GPIO_PIN_10 ((uint16_t)0x0400)
#define GPIO_PIN_11 ((uint16_t)0x0800)
#define GPIO_PIN_12 ((uint16_t)0x1000)
#define GPIO_PIN_13 ((uint16_t)0x2000)
#define GPIO_PIN_14 ((uint16_t)0x4000)
#define GPIO_PIN_15 ((uint16_t)0x8000)

typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
#define GPIO_MODE_OUTPUT_PP 1
#define GPIO_MODE_INPUT     0
#define GPIO_NOPULL   0
#define GPIO_PULLUP   1
#define GPIO_PULLDOWN 2
#define GPIO_SPEED_FREQ_LOW 0

void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *g);
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState s);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin);

#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)

typedef struct { void *Instance; } TIM_HandleTypeDef;
typedef struct { void *Instance; } UART_HandleTypeDef;
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);
int  HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *buf, uint16_t n);
#define __HAL_UART_CLEAR_OREFLAG(h) ((void)0)
#define __HAL_UART_CLEAR_NEFLAG(h)  ((void)0)
#define __HAL_UART_CLEAR_FEFLAG(h)  ((void)0)
#define __HAL_UART_CLEAR_PEFLAG(h)  ((void)0)

uint32_t HAL_GetTick(void);

static inline uint32_t __get_PRIMASK(void) { return 0; }
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t v) { (void)v; }

#define RELAY_LIGHT_Pin GPIO_PIN_7
#define RELAY_LIGHT_GPIO_Port GPIOB
#define GRIP_IN1_Pin GPIO_PIN_12
#define GRIP_IN1_GPIO_Port GPIOB
#define GRIP_IN2_Pin GPIO_PIN_13
#define GRIP_IN2_GPIO_Port GPIOB
#define GRIP_IN3_Pin GPIO_PIN_14
#define GRIP_IN3_GPIO_Port GPIOB
#define GRIP_IN4_Pin GPIO_PIN_15
#define GRIP_IN4_GPIO_Port GPIOB

#endif
