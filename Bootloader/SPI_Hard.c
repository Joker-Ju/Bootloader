#include "SPI_Hard.h"




void SPI_Hard_Init(void)
{
	// ── 时钟使能 ──
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN       // GPIOA
				| RCC_APB2ENR_SPI1EN;       // SPI1 外设

	// ── PA5(SCK) 复用推挽输出 ──
	GPIOA->CRL &= ~(GPIO_CRL_CNF5 | GPIO_CRL_MODE5);               // 清 CNF5+MODE5
	GPIOA->CRL |=  GPIO_CRL_CNF5_1 | GPIO_CRL_MODE5;               // CNF5=10(复用), MODE5=11(50MHz)

	// ── PA6(MISO) 浮空输入 ──
	GPIOA->CRL &= ~(GPIO_CRL_CNF6 | GPIO_CRL_MODE6);               // 清 CNF6+MODE6
	GPIOA->CRL |=  GPIO_CRL_CNF6_0;
	// CNF6=01(浮空输入), MODE6=00 → 默认就是输入，已经清完了

	// ── PA7(MOSI) 复用推挽输出 ──
	GPIOA->CRL &= ~(GPIO_CRL_CNF7 | GPIO_CRL_MODE7);
	GPIOA->CRL |=  GPIO_CRL_CNF7_1 | GPIO_CRL_MODE7;               // CNF7=10(复用), MODE7=11(50MHz)

	// ── PA1(CS) 通用推挽输出 ──
	GPIOA->CRL &= ~(GPIO_CRL_CNF1 | GPIO_CRL_MODE1);
	GPIOA->CRL |=  GPIO_CRL_MODE1;                // CNF1=00(通用), MODE1=11(50MHz)
	GPIOA->BSRR = (1 << 1);                   // CS 默认高（空闲）
	GPIOA->BSRR = (1 << 5);                   // SCK 默认高（CPOL=0 时空闲低，这里先拉高，SPI_Init 里会改为 CPOL=0）

	// ── SPI1 配置 ──
	// CR1 先清 0
	SPI1->CR1 = 0;

	// 按位配：
	// CPOL=0, CPHA=0  → Mode 0（和你的软件 SPI 一致）
	// MSTR=1          → 主机模式
	// BR[2:0]=011     → 分频 16（72MHz/16 = 4.5MHz，W25Q64 最大 50MHz 绰绰有余）
	// SSM=1, SSI=1    → 软件 NSS 管理（我们用 PA1 自己控 CS）
	// LSBFIRST=0      → MSB 先发
	// SPE=0           → 还没使能
	SPI1->CR1 &= ~(SPI_CR1_CPOL |SPI_CR1_CPHA);  // CPOL=0, CPHA=0
	SPI1->CR1 |= SPI_CR1_MSTR;
	SPI1->CR1 |= SPI_CR1_BR_0;    // 001 → 分频 4
	SPI1->CR1 |= SPI_CR1_SSM | SPI_CR1_SSI;     // 软件 NSS

	// ── 使能 SPI ──
	SPI1->CR1 |= SPI_CR1_SPE;
}



uint8_t SPI_Hard_SwapByte(uint8_t data)
{
	// 1. 等发送缓冲区空（TXE=1）
	while (!(SPI1->SR & SPI_SR_TXE));

	// 2. 写数据到 DR → 硬件自动发出 SCK 时钟
	SPI1->DR = data;

	// 3. 等接收缓冲区非空（RXNE=1）
	while (!(SPI1->SR & SPI_SR_RXNE));

	// 4. 读收到的字节
	return (uint8_t)SPI1->DR;
}


