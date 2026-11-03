#include "stm32f7xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LED_PIN             GPIO_PIN_5
#define LED_GPIO_PORT       GPIOB

#define VRX_CS_PIN          GPIO_PIN_6
#define VRX_CLK_PIN         GPIO_PIN_7
#define VRX_DATA_PIN        GPIO_PIN_9
#define VRX_GPIO_PORT       GPIOB

#define FLASH_SECTOR_11     11
#define FLASH_BASE_ADDR     0x081C0000 

typedef enum {
    MODE_STOP = 0,
    MODE_STROBE,
    MODE_RETRIC
} VGR_Mode;

typedef struct {
    uint32_t magic;
    uint32_t saved_mode;
    uint32_t last_frequency;
    float scan_speed;
    float battery_multiplier;
} __attribute__((packed)) NVRAM_Config;

UART_HandleTypeDef huart1; 
UART_HandleTypeDef huart3; 
ADC_HandleTypeDef  hadc1;
COMP_HandleTypeDef hcomp1;
DAC_HandleTypeDef  hdac1;
TIM_HandleTypeDef  htim2;

VGR_Mode current_mode = MODE_STOP;
uint32_t current_freq = 4900;
uint32_t hold_time_ms = 20;
float scan_speed_setting = 3.5;
float adc_calib_multiplier = 1.0f;

uint8_t rx_buffer[128];
uint8_t uart_byte;
uint16_t rx_index = 0;
uint8_t rx_state = 0;
uint8_t payload_len = 0;
uint8_t crc_received = 0;
char cmd_string[64];

uint32_t v_sync_period_us = 0;
uint8_t detected_video_standard = 0; // 0: None, 1: PAL (50Hz), 2: NTSC (60Hz)

void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_USART1_UART_Init(void);
void MX_USART3_UART_Init(void);
void MX_ADC1_Init(void);
void MX_DAC1_Init(void);
void MX_COMP1_Init(void);
void MX_TIM2_Init(void);
void DWT_Init(void);
void DWT_DelayUs(uint32_t us);
void Process_Secure_Command(char* cmd);
void Save_Config_To_Flash(void);
void Load_Config_From_Flash(void);
uint8_t Compute_CRC8(const uint8_t *data, uint8_t len);

int main(void) {
    // Смещение вектора прерываний под загрузчик Betaflight (48 КБ)
    SCB->VTOR = 0x0800C000;

    HAL_Init();
    SystemClock_Config();
    
    MX_GPIO_Init();
    DWT_Init();
    MX_USART1_UART_Init();
    MX_USART3_UART_Init();
    MX_ADC1_Init();
    MX_DAC1_Init();
    MX_COMP1_Init();
    MX_TIM2_Init();

    Load_Config_From_Flash();

    // Запуск внутреннего ЦАП для выдачи опорного напряжения 0.2 В на компаратор COMP1
    HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 250); 
    HAL_COMP_Start(&hcomp1);
    HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1);

    HAL_UART_Receive_IT(&huart3, &uart_byte, 1);
    uint32_t last_scan_tick = 0;
    uint32_t last_telemetry_tick = 0;

    while (1) {
        // Управление индикацией PB5
        if (current_mode == MODE_STOP) HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_SET);
        else if (current_mode == MODE_STROBE) {
            if (HAL_GetTick() % 150 < 40) HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_RESET);
            else HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_SET);
        } else if (current_mode == MODE_RETRIC) {
            if (HAL_GetTick() % 800 < 400) HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_RESET);
            else HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_SET);
        }

        // Кольцевой высокоскоростной перебор РЭР
        if (current_mode != MODE_STOP) {
            if (HAL_GetTick() - last_scan_tick >= hold_time_ms) {
                last_scan_tick = HAL_GetTick();
                current_freq += 25;
                if (current_freq > 5900) current_freq = 4900;

                // Перерасчет Hold Time на точку на основе ползунка скорости с веб-интерфейса
                // Базовый предел: не менее 10 мс для стабильного захвата PLL гетеродина
                uint32_t points = (5900 - 4900) / 25;
                hold_time_ms = (uint32_t)((scan_speed_setting * 1000.0f) / points);
                if (hold_time_ms < 10) hold_time_ms = 10;
            }
        }

        // Выстрел пакета телеметрии в пульт раз в секунду
        if (HAL_GetTick() - last_telemetry_tick >= 1000) {
            last_telemetry_tick = HAL_GetTick();
            HAL_ADC_Start(&hadc1);
            if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
                uint32_t raw_adc = HAL_ADC_GetValue(&hadc1);
                float voltage = ((float)raw_adc / 4095.0f) * 3.3f * 11.0f * adc_calib_multiplier;
                
                char tx_buf[128];
                sprintf(tx_buf, "TELEMETRY:V=%.2f,VRX=5.8G,VTX=5.8G\n", voltage);
                HAL_UART_Transmit(&huart3, (uint8_t*)tx_buf, strlen(tx_buf), 100);
            }
        }
    }
}

// Прерывание захвата таймера TIM2: Измерение периодов кадровых синхроимпульсов
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM2 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        static uint32_t last_capture = 0;
        uint32_t current_capture = HAL_TIM_ReadCapturedValue(&htim2, TIM_CHANNEL_1);
        
        if (current_capture > last_capture) {
            v_sync_period_us = current_capture - last_capture;
        } else {
            v_sync_period_us = (0xFFFFFFFF - last_capture) + current_capture;
        }
        last_capture = current_capture;

        // Фильтрация частоты кадров видеосигнала
        if (v_sync_period_us >= 19800 && v_sync_period_us <= 20200) {
            detected_video_standard = 1; // Захвачен поток PAL (50 полукадров/сек -> 20000 мкс)
        } else if (v_sync_period_us >= 16400 && v_sync_period_us <= 16800) {
            detected_video_standard = 2; // Захвачен поток NTSC (60 полукадров/сек -> 16666 мкс)
        } else {
            detected_video_standard = 0; // Неопределенный эфирный шум
        }
    }
}

// Помехозащищенный конечный автомат приема пакетов UART3 с верификацией по CRC8
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART3) {
        switch (rx_state) {
            case 0: // Ожидание стартового символа '$'
                if (uart_byte == '$') rx_state = 1;
                break;
            case 1: // Чтение длины полезной нагрузки
                payload_len = uart_byte;
                rx_index = 0;
                if (payload_len < 64) rx_state = 2;
                else rx_state = 0;
                break;
            case 2: // Чтение строки данных
                cmd_string[rx_index++] = uart_byte;
                if (rx_index >= payload_len) {
                    cmd_string[rx_index] = '\0';
                    rx_state = 3;
                }
                break;
            case 3: // Чтение контрольной суммы CRC8
                crc_received = uart_byte;
                rx_state = 4;
                break;
            case 4: // Проверка терминатора строки '\n'
                if (uart_byte == '\n') {
                    if (Compute_CRC8((uint8_t*)cmd_string, payload_len) == crc_received) {
                        Process_Secure_Command(cmd_string);
                    }
                }
                rx_state = 0;
                break;
        }
        HAL_UART_Receive_IT(&huart3, &uart_byte, 1);
    }
}

void Process_Secure_Command(char* cmd) {
    char mode_str[32] = {0};
    float speed_val = 0.0f;
    float cal_val = 0.0f;

    if (sscanf(cmd, "MODE:%31s", mode_str) == 1) {
        if (strcmp(mode_str, "СТРОБОСКОП") == 0) current_mode = MODE_STROBE;
        else if (strcmp(mode_str, "РЕТРИК") == 0) current_mode = MODE_RETRIC;
        else current_mode = MODE_STOP;
        Save_Config_To_Flash();
    }
    else if (sscanf(cmd, "SPEED:%f", &speed_val) == 1) {
        if (speed_val >= 2.0f && speed_val <= 5.0f) {
            scan_speed_setting = speed_val;
            Save_Config_To_Flash();
        }
    }
    else if (sscanf(cmd, "CALIB:%f", &cal_val) == 1) {
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
            uint32_t raw_adc = HAL_ADC_GetValue(&hadc1);
            float cur_v = ((float)raw_adc / 4095.0f) * 3.3f * 11.0f;
            adc_calib_multiplier = cal_val / cur_v;
            Save_Config_To_Flash();
        }
    }
}

uint8_t Compute_CRC8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else crc <<= 1;
        }
    }
    return crc;
}

void Save_Config_To_Flash(void) {
    NVRAM_Config cfg;
    cfg.magic = 0x56475231;
    cfg.saved_mode = (uint32_t)current_mode;
    cfg.last_frequency = current_freq;
    cfg.scan_speed = scan_speed_setting;
    cfg.battery_multiplier = adc_calib_multiplier;

    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef erase_struct;
    erase_struct.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_struct.Sector = FLASH_SECTOR_11;
    erase_struct.NbSectors = 1;
    erase_struct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    
    uint32_t error = 0;
    if (HAL_FLASHEx_Erase(&erase_struct, &error) == HAL_OK) {
        uint32_t *data_ptr = (uint32_t*)&cfg;
        for (uint32_t i = 0; i < sizeof(NVRAM_Config) / 4; i++) {
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_BASE_ADDR + (i * 4), data_ptr[i]);
        }
    }
    HAL_FLASH_Lock();
}

void Load_Config_From_Flash(void) {
    NVRAM_Config cfg;
    memcpy(&cfg, (void*)FLASH_BASE_ADDR, sizeof(NVRAM_Config));
    if (cfg.magic == 0x56475231) {
        current_mode = (VGR_Mode)cfg.saved_mode;
        current_freq = cfg.last_frequency;
        scan_speed_setting = cfg.scan_speed;
        adc_calib_multiplier = cfg.battery_multiplier;
    } else {
        current_mode = MODE_STOP;
        current_freq = 4900;
        scan_speed_setting = 3.5f;
        adc_calib_multiplier = 1.0f;
    }
}

void DWT_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void DWT_DelayUs(uint32_t us) {
    uint32_t startTick = DWT->CYCCNT;
    uint32_t delayTicks = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - startTick) < delayTicks);
}

void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = VRX_CS_PIN | VRX_CLK_PIN | VRX_DATA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(VRX_GPIO_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(VRX_GPIO_PORT, VRX_CS_PIN, GPIO_PIN_SET);
}

void MX_USART1_UART_Init(void) {
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 4800;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX;
    HAL_HalfDuplex_Init(&huart1);
}

void MX_USART3_UART_Init(void) {
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 115200;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    HAL_UART_Init(&huart3);
}

void MX_ADC1_Init(void) {
    ADC_ChannelConfTypeDef sConfig = {0};
    __HAL_RCC_ADC1_CLK_ENABLE();
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCKPRESCALER_PCLK2_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    HAL_ADC_Init(&hadc1);

    sConfig.Channel = ADC_CHANNEL_1; // PA1
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_56CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

void MX_DAC1_Init(void) {
    DAC_ChannelConfTypeDef sConfig = {0};
    __HAL_RCC_DAC_CLK_ENABLE();
    hdac1.Instance = DAC1;
    HAL_DAC_Init(&hdac1);
    sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1); // PA4
}

void MX_COMP1_Init(void) {
    __HAL_RCC_COMP_CLK_ENABLE();
    hcomp1.Instance = COMP1;
    hcomp1.Init.InvertingInput = COMP_INPUT_MINUS_DAC1_CH1; // Опорный уровень от DAC1
    hcomp1.Init.NonInvertingInput = COMP_INPUT_PLUS_IO1;    // Сигнал Video Out на PA4
    hcomp1.Init.Output = COMP_OUTPUT_TIM2_IC1;             // Аппаратное перенаправление на TIM2 CH1
    hcomp1.Init.OutputPol = COMP_OUTPUTPOL_NONINVERTED;
    hcomp1.Init.Hysteresis = COMP_HYSTERESIS_MEDIUM;
    hcomp1.Init.BlankingSR = COMP_BLANKINGSR_NONE;
    hcomp1.Init.Mode = COMP_MODE_HIGHSPEED;
    HAL_COMP_Init(&hcomp1);
}

void MX_TIM2_Init(void) {
    TIM_IC_InitTypeDef sConfigIC = {0};
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 216 - 1; // Тактирование таймера с шагом ровно 1 мкс (при частоте шины 216 МГц)
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFFFFFF;
    htim2.Init.ClockDivision = TIM_CLOCKREVISION_DIV1;
    HAL_TIM_IC_Init(&htim2);

    sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
    sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
    sConfigIC.ICFilter = 4; 
    HAL_TIM_IC_ConfigChannel(&htim2, &sConfigIC, TIM_CHANNEL_1);
}

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 432;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    HAL_PWREx_EnableOverDrive();
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7);
}
