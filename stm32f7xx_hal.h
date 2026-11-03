#ifndef __STM32F7xx_HAL_H
#define __STM32F7xx_HAL_H

#include "stm32f7xx.h"

#define FLASH_LATENCY_7          ((uint32_t)0x00000007U)
#define RCC_SYSCLK_DIV1          ((uint32_t)0x00000000U)
#define RCC_HCLK_DIV2            ((uint32_t)0x00008000U)
#define RCC_HCLK_DIV4            ((uint32_t)0x00009000U)
#define RCC_SYSCLKSOURCE_PLLCLK  ((uint32_t)0x00000002U)

#define RCC_CLOCKTYPE_SYSCLK     ((uint32_t)0x00000001U)
#define RCC_CLOCKTYPE_HCLK       ((uint32_t)0x00000002U)
#define RCC_CLOCKTYPE_PCLK1      ((uint32_t)0x00000004U)
#define RCC_CLOCKTYPE_PCLK2      ((uint32_t)0x00000008U)

#define RCC_OSCILLATORTYPE_HSE   ((uint32_t)0x00000001U)
#define RCC_HSE_ON               ((uint32_t)0x00000020U)
#define RCC_HSI_OFF              ((uint32_t)0x00000000U)
#define RCC_PLL_ON               ((uint32_t)0x00000002U)
#define RCC_PLLSOURCE_HSE        ((uint32_t)0x00000004U)
#define RCC_PLLP_DIV2            ((uint32_t)0x00000002U)

#define PWR_REGULATOR_VOLTAGE_SCALE1  ((uint32_t)0x00000003U)
#define __HAL_RCC_PWR_CLK_ENABLE()    do { } while(0)
#define __HAL_PWR_VOLTAGESCALING_CONFIG(value) do { } while(0)

// Константы для настройки таймеров STM32F722
#define TIM_ICPSC_DIV1           ((uint32_t)0x00000000U)
#define TIM_CHANNEL_1            ((uint32_t)0x00000000U)

typedef enum {
  HAL_OK       = 0x00U,
  HAL_ERROR    = 0x01U,
  HAL_BUSY     = 0x02U,
  HAL_TIMEOUT  = 0x03U
} HAL_StatusTypeDef;

typedef struct {
  uint32_t PLLState;
  uint32_t PLLSource;
  uint32_t PLLM;
  uint32_t PLLN;
  uint32_t PLLP;
  uint32_t PLLQ;
} RCC_PLLInitTypeDef;

typedef struct {
  uint32_t OscillatorType;
  uint32_t HSEState;
  uint32_t LSEState;
  uint32_t HSIState;
  uint32_t HSICalibrationValue;
  uint32_t LSIState;
  RCC_PLLInitTypeDef PLL; 
} RCC_OscInitTypeDef;

typedef struct {
  uint32_t ClockType;
  uint32_t SYSCLKSource;
  uint32_t AHBCLKDivider;
  uint32_t APB1CLKDivider;
  uint32_t APB2CLKDivider;
} RCC_ClkInitTypeDef;

// Структуры для конфигурации таймеров захвата сигнала дрона
typedef struct {
  uint32_t ICPolarity;
  uint32_t ICSelection;
  uint32_t ICPrescaler;
  uint32_t ICFilter;
} TIM_IC_InitTypeDef;

HAL_StatusTypeDef HAL_Init(void);
void HAL_GPIO_Init(void *GPIOx, void *GPIO_Init);
void HAL_Delay(uint32_t Delay);

void HAL_RCC_OscConfig(RCC_OscInitTypeDef *RCC_OscInitStruct);
void HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *RCC_ClkInitStruct, uint32_t FLatency);
HAL_StatusTypeDef HAL_PWREx_EnableOverDrive(void);

// Функция инициализации каналов таймера
HAL_StatusTypeDef HAL_TIM_IC_ConfigChannel(void *htim, TIM_IC_InitTypeDef *sConfig, uint32_t Channel);

#endif
