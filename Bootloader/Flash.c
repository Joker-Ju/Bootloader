#include "Flash.h"


void Flash_unlock(void) 
{
	FLASH->KEYR = 0x45670123;  // 钥匙1
	FLASH->KEYR = 0xCDEF89AB;  // 钥匙2
	// 如果解锁成功，CR 的 LOCK 位会变为 0
}

void Flash_lock(void) 
{
	FLASH->CR |= FLASH_CR_LOCK;  // 置 LOCK 位 = 上锁
}

uint32_t Flash_erase_page(uint32_t page_addr)
{
	while (FLASH->SR & FLASH_SR_BSY);
	
	FLASH->CR |= FLASH_CR_PER;
	FLASH->AR = page_addr;
	FLASH->CR |= FLASH_CR_STRT;
	
	while (FLASH->SR & FLASH_SR_BSY);
	//读取状态，检查是否成功
	uint32_t sr = FLASH->SR;
	
	FLASH->CR &= ~FLASH_CR_PER;

	return (sr & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) ? 1 : 0;
}

uint32_t Flash_write_halfword(uint32_t addr, uint16_t data)
{
	while (FLASH->SR & FLASH_SR_BSY);
	FLASH->CR |= FLASH_CR_PG;		//进入编程模式
	*(volatile uint16_t *)addr = data;
	while (FLASH->SR & FLASH_SR_BSY);
	uint32_t sr = FLASH->SR;
	FLASH->CR &= ~FLASH_CR_PG;		//退出编程模式
	return (sr & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) ? 1 : 0;
}

uint32_t Flash_write_data(uint32_t addr, uint8_t *data, uint32_t size)
{
	uint32_t i;
	uint16_t hw;
	
	for (i = 0; i < size; i += 2) 
	{
	// 把两个字节拼成一个半字（小端序）
		hw = data[i];                    // 低字节
		if (i + 1 < size) {
			hw |= (data[i + 1] << 8);   // 高字节
		} 
		else {
			hw |= 0xFF00;               // 最后一个字节，补 0xFF（不改变 Flash 原有值）
		}

		if (Flash_write_halfword(addr + i, hw) != 0) {
				return 1;  // 写失败
		}
	}
	return 0;
}

uint32_t Flash_erase_app_area(uint32_t start_addr, uint32_t size)  // 擦除 App 区域
{
	uint32_t pages = (size + 1023) / 1024;  // 向上取整到整页数

	for (uint32_t p = 0; p < pages; p++) {
		if (Flash_erase_page(start_addr + p * 1024) != 0) {
			Flash_lock();
			return 1;  // 擦除失败
		}
	}

	return 0;	
}

