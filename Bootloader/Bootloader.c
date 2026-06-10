#include "Flash.h"     // Flash 操作
#include "usart.h"	 // USART 输出
#include "W25Q64_Reader.h"


// ── 地址定义 ──
#define FLAG_ADDR       0x0800E000
#define BOOT_TO_A       0xAAAAAAAA
#define BOOT_TO_B       0xBBBBBBBB


#define APP_A_ADDR      0x08002000
#define APP_B_ADDR      0x08008000
void jump_to_app(uint32_t app_addr);

#define HSE_VALUE ((uint32_t)8000000)

void SystemInit(void)
{
	// 复位 RCC
	RCC->CR |= 0x00000001;
	RCC->CFGR &= 0xF8FF0000;
	RCC->CR &= 0xFEF6FFFF;
	RCC->CR &= 0xFFFBFFFF;
	RCC->CFGR &= 0xFF80FFFF;
	RCC->CIR = 0x009F0000;

	// 配成 72MHz：HSE 晶振 → PLL ×9 → SYSCLK
	RCC->CR |= RCC_CR_HSEON;
	while (!(RCC->CR & RCC_CR_HSERDY));

	FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

	RCC->CFGR = RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLMULL9
			| RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;

	RCC->CR |= RCC_CR_PLLON;
	while (!(RCC->CR & RCC_CR_PLLRDY));

	RCC->CFGR |= RCC_CFGR_SW_PLL;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

	// VTOR 不改（Bootloader 的向量表就在 0x08000000）
	SCB->VTOR = FLASH_BASE;
}

uint32_t crc32_calc(const uint8_t *data, uint32_t len) {
	uint32_t crc = 0xFFFFFFFF;
	for (uint32_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int j = 0; j < 8; j++) {
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320;
			else
				crc >>= 1;
		}
	}
	return crc ^ 0xFFFFFFFF;
}

// ── App 有效性检查 ──
uint32_t is_app_valid(uint32_t app_addr) {
	uint32_t code_len  = 24 * 1024 - 8;
	uint32_t crc_addr  = app_addr + code_len;

	uint32_t stored_crc = *(volatile uint32_t *)crc_addr;

	// 快速过滤：空分区（全是 0xFF，CRC 不会是 0xFFFFFFFF）
	if (stored_crc == 0xFFFFFFFF)
		return 0;

	uint32_t computed = crc32_calc((uint8_t *)app_addr, code_len);
	return (computed == stored_crc) ? 1 : 0;
}

// ── GPIO 初始化（PC13=LED） ──
void gpio_init(void) {
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN;

	// PC13: 推挽输出（LED）
	GPIOC->CRH &= ~(GPIO_CRH_CNF13 | GPIO_CRH_MODE13);           // 清除 CNF13、MODE13
	GPIOC->CRH |= GPIO_CRH_MODE13;            // MODE13=11, CNF13=00 → 推挽输出 50MHz
}

void led_on(void)  { GPIOC->ODR &= ~(1 << 13); }  // PC13=0 → LED 亮
void led_off(void) { GPIOC->ODR |= (1 << 13); }   // PC13=1 → LED 灭


 // ── OTA 升级（从 W25Q64 写到内部 Flash） ──
 static void ota_update(void) {
	W25Q64_Init();
 	uart_print("OTA: Reading W25Q64...\r\n");

 	// 1. 读固件大小（App 下载完成后写到了 0x00FFFC）
 	uint32_t fw_size;
 	W25Q64_Read(0x00FFFC, (uint8_t*)&fw_size, 4);

 	if (fw_size == 0 || fw_size > 48 * 1024) {
 		uart_print("OTA: Invalid size!\r\n");
 		return;
 	}

 	// 2. 判断目标分区
 	uint32_t flag = *(volatile uint32_t *)FLAG_ADDR;
 	uint32_t target = (flag == BOOT_TO_A) ? APP_B_ADDR : APP_A_ADDR;

 	uart_print(target == APP_B_ADDR ? "OTA: A -> B\r\n" : "OTA: B -> A\r\n");

 	// 3. 擦除目标分区
 	Flash_unlock();
 	Flash_erase_app_area(target, fw_size);

 	// 4. 逐页复制 W25Q64 → 内部 Flash
 	uint8_t page[256];
 	uint32_t w25_addr = 0;
 	while (w25_addr < fw_size) {
 		uint32_t chunk = (fw_size - w25_addr >= 256) ? 256 : (fw_size - w25_addr);
 		W25Q64_Read(w25_addr, page, chunk);
 		Flash_write_data(target + w25_addr, page, chunk);
 		w25_addr += 256;
 	}
	// ═══  加 CRC 校验  ═══
	if (!is_app_valid(target)) {
		uart_print("OTA: CRC FAILED!\r\n");
		Flash_lock();
		return;                     // 不翻转标志，不跳转，下次还进原来分区
	}
	
 	// 5. 翻转分区标志
 	Flash_erase_page(FLAG_ADDR);
 	uint32_t new_flag = (target == APP_B_ADDR) ? BOOT_TO_B : BOOT_TO_A;
 	Flash_write_halfword(FLAG_ADDR,    new_flag & 0xFFFF);
 	Flash_write_halfword(FLAG_ADDR+2, (new_flag >> 16) & 0xFFFF);
 	Flash_lock();

 	// 6. BKP 未确认 + 跳转
 	PWR->CR |= PWR_CR_DBP;
 	BKP->DR4 = 0xFFFF;
 	jump_to_app(target);
 }


// ── 回滚检查 ──
// 当前分区 App 未确认（BKP->DR4 != 0x0000）时，切到另一分区
static void check_rollback(uint32_t other_addr, uint32_t other_flag) {
	if (BKP->DR4 == 0x0000) return;         // 已确认，不用回滚

	uart_print(" unconfirmed! Rollback\r\n");

	Flash_unlock();
	Flash_erase_page(FLAG_ADDR);
	Flash_write_halfword(FLAG_ADDR,    other_flag & 0xFFFF);
	Flash_write_halfword(FLAG_ADDR+2, (other_flag >> 16) & 0xFFFF);
	Flash_lock();
	BKP->DR4 = 0;

	if (is_app_valid(other_addr)) {
		uart_print("Back to other\r\n");
		jump_to_app(other_addr);
	}
	// 另一个区也无效 → 继续往下走，试当前区
}



// ── main ──
int main(void) {
	uint32_t flag;
	uart_init();
	gpio_init();
	led_on();
	uart_print("\r\n===== Bootloader =====\r\n");
	
	// 先检查 BKP（App 请求的升级）
	RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
	PWR->CR |= PWR_CR_DBP;       // ← 加这行：开启后备域写权限
	if (BKP->DR1 == 0x5555) {
      uart_print("OTA request\r\n");
      BKP->DR1 = 0;               // 清标记
      ota_update();               // 读 W25Q64 → 写 Flash → 跳转
      // ota_update 里会跳转，不会回到这
  	}
	
	flag = *(volatile uint32_t *)FLAG_ADDR;
	// 读标志区
	if (flag == BOOT_TO_A) {
		// 回滚检查：如果 DR4 != 0x0000，说明新固件没确认过
		uart_print("A");
		check_rollback(APP_B_ADDR, BOOT_TO_B);
		uart_print("Boot to A\r\n");
		if (is_app_valid(APP_A_ADDR)) {
			jump_to_app(APP_A_ADDR);
		}
	} 
	else if (flag == BOOT_TO_B) {
		uart_print("B");
		check_rollback(APP_A_ADDR, BOOT_TO_A);
		uart_print("Boot to B\r\n");
		if (is_app_valid(APP_B_ADDR)) {
			jump_to_app(APP_B_ADDR);
		}
	}
	
	uart_print("Fallback: try A\r\n");
	// 没有有效标志或 App，检查 A 区兜底
	if (is_app_valid(APP_A_ADDR)) {
		jump_to_app(APP_A_ADDR);
	}

	// uart_print("No valid app, wait for XMODEM...\r\n");
	// upgrade_to(APP_A_ADDR);
	while (1) {
		led_on();
			for (volatile uint32_t i = 0; i < 2000000; i++);
		led_off();
			for (volatile uint32_t i = 0; i < 2000000; i++);
		// 在这里初始化 USART，等固件…
	}
}


