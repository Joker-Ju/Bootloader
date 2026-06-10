// ── W25Q64_Reader.h ──
#ifndef W25Q64_READER_H
#define W25Q64_READER_H

#include "W25Q64_Ins.h"
#include "SPI_Hard.h"

void    W25Q64_Init(void);
void    W25Q64_Read(uint32_t addr, uint8_t *buf, uint32_t len);
uint8_t W25Q64_ReadID(void);

#endif
