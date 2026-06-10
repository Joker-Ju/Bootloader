#include "W25Q64_Reader.h"

#define SPI_Start_pa1()     do { SPI_Hard_Start(GPIOA, 1); } while(0)
#define SPI_Stop_pa1()      do { SPI_Hard_Stop(GPIOA, 1); } while(0)



void W25Q64_Init(void)
{
	SPI_Hard_Init();
	for (volatile uint32_t i = 0; i < 72000; i++);
}

void W25Q64_Read(uint32_t addr, uint8_t *buf, uint32_t len) {
	SPI_Start_pa1();
	SPI_Hard_SwapByte(W25Q64_READ_DATA);
	SPI_Hard_SwapByte((addr >> 16) & 0xFF);
	SPI_Hard_SwapByte((addr >> 8)  & 0xFF);
	SPI_Hard_SwapByte(addr        & 0xFF);
	for (uint32_t i = 0; i < len; i++) {
		buf[i] = SPI_Hard_SwapByte(W25Q64_DUMMY_BYTE);
	}
	SPI_Stop_pa1();
}
