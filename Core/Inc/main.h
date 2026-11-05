/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

#include "stm32f4xx_nucleo.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define A1_MAX_Pin GPIO_PIN_1
#define A1_MAX_GPIO_Port GPIOC
#define A1_STEP_Pin GPIO_PIN_0
#define A1_STEP_GPIO_Port GPIOA
#define A1_DIR_Pin GPIO_PIN_1
#define A1_DIR_GPIO_Port GPIOA
#define A1_EN_Pin GPIO_PIN_4
#define A1_EN_GPIO_Port GPIOA
#define A2_MAX_Pin GPIO_PIN_6
#define A2_MAX_GPIO_Port GPIOA
#define A2_MIN_Pin GPIO_PIN_7
#define A2_MIN_GPIO_Port GPIOA
#define A4_DIR_Pin GPIO_PIN_5
#define A4_DIR_GPIO_Port GPIOC
#define A1_MIN_Pin GPIO_PIN_0
#define A1_MIN_GPIO_Port GPIOB
#define A3_MIN_Pin GPIO_PIN_10
#define A3_MIN_GPIO_Port GPIOB
#define GRIP_IN1_Pin GPIO_PIN_12
#define GRIP_IN1_GPIO_Port GPIOB
#define GRIP_IN2_Pin GPIO_PIN_13
#define GRIP_IN2_GPIO_Port GPIOB
#define GRIP_IN3_Pin GPIO_PIN_14
#define GRIP_IN3_GPIO_Port GPIOB
#define GRIP_IN4_Pin GPIO_PIN_15
#define GRIP_IN4_GPIO_Port GPIOB
#define A4_STEP_Pin GPIO_PIN_6
#define A4_STEP_GPIO_Port GPIOC
#define A2_DIR_Pin GPIO_PIN_7
#define A2_DIR_GPIO_Port GPIOC
#define A4_EN_Pin GPIO_PIN_8
#define A4_EN_GPIO_Port GPIOC
#define A4_MIN_Pin GPIO_PIN_9
#define A4_MIN_GPIO_Port GPIOC
#define A3_MAX_Pin GPIO_PIN_8
#define A3_MAX_GPIO_Port GPIOA
#define A2_STEP_Pin GPIO_PIN_9
#define A2_STEP_GPIO_Port GPIOA
#define A3_STEP_Pin GPIO_PIN_10
#define A3_STEP_GPIO_Port GPIOA
#define A3_EN_Pin GPIO_PIN_4
#define A3_EN_GPIO_Port GPIOB
#define A3_DIR_Pin GPIO_PIN_5
#define A3_DIR_GPIO_Port GPIOB
#define A2_EN_Pin GPIO_PIN_6
#define A2_EN_GPIO_Port GPIOB
#define RELAY_LIGHT_Pin GPIO_PIN_7
#define RELAY_LIGHT_GPIO_Port GPIOB
#define A4_MAX_Pin GPIO_PIN_8
#define A4_MAX_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
