#include "usart.h"




void uart_init(void) {
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

	GPIOA->CRH |= GPIO_CRH_CNF9_1;     // PA9 复用推挽
	GPIOA->CRH &= ~GPIO_CRH_CNF9_0;
	GPIOA->CRH |= GPIO_CRH_MODE9;

	GPIOA->CRH &= ~GPIO_CRH_CNF10_1;   // PA10 浮空输入
	GPIOA->CRH |= GPIO_CRH_CNF10_0;
	GPIOA->CRH &= ~GPIO_CRH_MODE10;

	USART1->BRR = 72000000 / 115200;
	USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}
void uart_putchar(uint8_t c) {
	while (!(USART1->SR & USART_SR_TXE));
	USART1->DR = c;
}


void uart_print(const char *s) {
	while (*s) uart_putchar(*s++);
}
