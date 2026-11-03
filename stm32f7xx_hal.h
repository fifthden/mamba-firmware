#ifndef __STM32F7xx_HAL_H
#define __STM32F7xx_HAL_H

#include "stm32f7xx.h"

typedef enum {
  HAL_OK       = 0x00U,
  HAL_ERROR    = 0x01U,
  HAL_BUSY     = 0x02U,
  HAL_TIMEOUT  = 0x03U
} HAL_StatusTypeDef;

HAL_StatusTypeDef HAL_Init(void);
void HAL_GPIO_Init(void *GPIOx, void *GPIO_Init);
void HAL_Delay(uint32_t Delay);

#endif
