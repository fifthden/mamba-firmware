#ifndef __STM32F7xx_HAL_H
#define __STM32F7xx_HAL_H

#include "stm32f7xx.h"

#define FLASH_LATENCY_7          ((uint32_t)0x00000007U)
#define RCC_SYSCLK_DIV1          ((uint32_t)0x00000000U)
#define RCC_HCLK_DIV2            ((uint32_t)0x00008000U)
#define RCC_HCLK_DIV4            ((uint32_t)0x00009000U)
#define RCC_SYSCLKSOURCE_PLLCLK  ((uint32_t)0x00000002U)

typedef enum {
  HAL_OK       = 0x00U,
  HAL_ERROR    = 0x01U,
  HAL_BUSY     = 0x02U,
  HAL_TIMEOUT  = 0x03U
} HAL_StatusTypeDef;

typedef struct {
  uint32_t OscillatorType;
  uint32_t HSEState;
  uint32_t LSEState;
  uint32_t HSIState;
  uint32_t HSICalibrationValue;
  uint32_t LSIState;
  void*    PLL; 
} RCC_OscInitTypeDef;

typedef struct {
  uint32_t ClockType;
  uint32_t SYSCLKSource;
  uint32_t AHBCLKDivider;
  uint32_t APB1CLKDivider;
  uint32_t APB2CLKDivider;
} RCC_ClkInitTypeDef;

HAL_StatusTypeDef HAL_Init(void);
void HAL_GPIO_Init(void *GPIOx, void *GPIO_Init);
void HAL_Delay(uint32_t Delay);

void HAL_RCC_OscConfig(RCC_OscInitTypeDef *RCC_OscInitStruct);
void HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *RCC_ClkInitStruct, uint32_t FLatency);

#endif
