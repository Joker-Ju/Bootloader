#ifndef USART_H
#define USART_H

#include "stm32f10x.h"

void uart_init(void);
void uart_putchar(uint8_t c);
void uart_print(const char *s);

#endif // USART_H
