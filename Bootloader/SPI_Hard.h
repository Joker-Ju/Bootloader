#ifndef __SPI_HARD_H
#define __SPI_HARD_H

#include "stm32f10x.h"

// CS 控制宏（SPI1，PA4，和软件版一样用法）
#define SPI_Hard_Start(port, pin)    do { (port)->BRR  = (1 << (pin)); } while(0)
#define SPI_Hard_Stop(port, pin)     do { (port)->BSRR = (1 << (pin)); } while(0)

void     SPI_Hard_Init(void);
uint8_t  SPI_Hard_SwapByte(uint8_t data);



#endif
