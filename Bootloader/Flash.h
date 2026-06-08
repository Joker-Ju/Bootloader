#ifndef FLASH_H
#define FLASH_H

#include "stm32f10x.h"

#define APP_START_ADDR  0x08002000
#define APP_MAX_SIZE    (56 * 1024)  // 56KB


void Flash_unlock(void);
void Flash_lock(void);
uint32_t Flash_erase_page(uint32_t page_addr);
uint32_t Flash_write_halfword(uint32_t addr, uint16_t data);
uint32_t Flash_write_data(uint32_t addr, uint8_t *data, uint32_t size);
uint32_t Flash_erase_app_area(uint32_t start_addr, uint32_t size);  // 擦除 App 区域

#endif
