#ifndef __STM32F722xx_H
#define __STM32F722xx_H

#include <stdint.h>

#define PERIPH_BASE           ((uint32_t)0x40000000)
#define AHB1PERIPH_BASE       (PERIPH_BASE + 0x00020000)
#define GPIOA_BASE            (AHB1PERIPH_BASE + 0x0000)
#define GPIOB_BASE            (AHB1PERIPH_BASE + 0x0400)
#define GPIOC_BASE            (AHB1PERIPH_BASE + 0x0800)

typedef struct {
  volatile uint32_t MODER;    
  volatile uint32_t OTYPER;   
  volatile uint32_t OSPEEDR;  
  volatile uint32_t PUPDR;    
  volatile uint32_t IDR;      
  volatile uint32_t ODR;      
  volatile uint32_t BSRR;     
  volatile uint32_t LCKR;     
  volatile uint32_t AFR[2];   
} GPIO_TypeDef;

#define GPIOA               ((GPIO_TypeDef *) GPIOA_BASE)
#define GPIOB               ((GPIO_TypeDef *) GPIOB_BASE)
#define GPIOC               ((GPIO_TypeDef *) GPIOC_BASE)

typedef struct {
  volatile uint32_t VTOR;
} SCB_Type;
#define SCS_BASE            ((uint32_t)0xE000E000)
#define SCB_BASE            (SCS_BASE +  0x0D00)
#define SCB                 ((SCB_Type       *)     SCB_BASE)

#endif
