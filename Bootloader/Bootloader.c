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

void jump_to_app_safe(uint32_t app_addr){
	SysTick->CTRL = 0;
	SCB->ICSR = SCB_ICSR_PENDSTCLR;
	jump_to_app(app_addr);
}


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



// ── GPIO 初始化（PC13=LED） ──
void gpio_init(void) {
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN;

	// PC13: 推挽输出（LED）
	GPIOC->CRH &= ~(GPIO_CRH_CNF13 | GPIO_CRH_MODE13);           // 清除 CNF13、MODE13
	GPIOC->CRH |= GPIO_CRH_MODE13;            // MODE13=11, CNF13=00 → 推挽输出 50MHz
}

void led_on(void)  { GPIOC->ODR &= ~(1 << 13); }  // PC13=0 → LED 亮
void led_off(void) { GPIOC->ODR |= (1 << 13); }   // PC13=1 → LED 灭


static uint32_t g_fw_size;

// ── OTA 升级（从 W25Q64 写到内部 Flash） ──
static void ota_update(void) {
	W25Q64_Init();
	uart_print("OTA: Reading W25Q64...\r\n");

	// 1. 读固件大小
	if (g_fw_size < 128 || g_fw_size > 48 * 1024) {
		uart_print("OTA: Invalid size!\r\n");
		return;
	}

	// 2. 判断目标分区
	uint32_t cur_flag = *(volatile uint32_t *)FLAG_ADDR;
	uint32_t target = (cur_flag == BOOT_TO_A) ? APP_B_ADDR : APP_A_ADDR;

	uart_print(target == APP_B_ADDR ? "OTA: A -> B\r\n" : "OTA: B -> A\r\n");

	// 3. 擦除目标分区
	Flash_unlock();
	Flash_erase_app_area(target, g_fw_size);

	// 4. 逐页复制 W25Q64 → 内部 Flash
	static uint8_t page[256];
	uint32_t w25_addr = 0;
	while (w25_addr < g_fw_size) {
		uint32_t chunk = (g_fw_size - w25_addr >= 256) ? 256 : (g_fw_size - w25_addr);
		W25Q64_Read(w25_addr, page, chunk);
		Flash_write_data(target + w25_addr, page, chunk);
		w25_addr += 256;
	}

	// 读 W25Q64 里的预期 CRC（在 g_fw_size 偏移处）
	uint32_t expected_crc;
	W25Q64_Read(g_fw_size, (uint8_t*)&expected_crc, 4);

	// 直接算 CRC
	uint32_t computed = crc32_calc((uint8_t*)target, g_fw_size);
	if (computed != expected_crc) {
		uart_print("OTA: CRC FAILED!\r\n");
		uart_print("OTA: computed = 0x");
		char hexbuf[9];
		for (int i = 7; i >= 0; i--) {
			uint8_t nibble = (computed >> (i * 4)) & 0x0F;
			hexbuf[7 - i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
		}
		hexbuf[8] = '\0';
		uart_print(hexbuf);
		uart_print(" expected = 0x");
		for (int i = 7; i >= 0; i--) {
			uint8_t nibble = (expected_crc >> (i * 4)) & 0x0F;
			hexbuf[7 - i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
		}
		hexbuf[8] = '\0';
		uart_print(hexbuf);
		uart_print("\r\n");
		Flash_lock();
		return;
	}
	uart_print("OTA: CRC OK\r\n");
	// 5. 翻转分区标志
	Flash_erase_page(FLAG_ADDR);
	uint32_t new_flag = (target == APP_B_ADDR) ? BOOT_TO_B : BOOT_TO_A;
	Flash_write_halfword(FLAG_ADDR,    new_flag & 0xFFFF);
	Flash_write_halfword(FLAG_ADDR+2, (new_flag >> 16) & 0xFFFF);
	Flash_lock();

	// 6. BKP 未确认 + 跳转
	PWR->CR |= PWR_CR_DBP;
	BKP->DR4 = 0xFFFF;
	SysTick->CTRL = 0;
	SCB->ICSR = SCB_ICSR_PENDSTCLR;
	jump_to_app_safe(target);
}

static uint32_t is_app_bootable(uint32_t app_addr) {
	uint32_t sp   = *(volatile uint32_t *)app_addr;
	uint32_t pc   = *(volatile uint32_t *)(app_addr + 4);

	if (sp < 0x20000000 || sp > 0x20005000 || pc < 0x08000000 || pc > 0x08010000) return 0;
	// LSB 必须是 1（Thumb 模式）
	if ((pc & 1) == 0) return 0;
	return 1;
}
// ── 回滚检查 ──
// 当前分区 App 未确认（BKP->DR4 != 0x0000）时，切到另一分区
static void check_rollback(uint32_t other_addr, uint32_t other_flag) {
	if (BKP->DR4 == 0x0000) {
		uart_print(other_flag == BOOT_TO_B ? "Boot to A\r\n" : "Boot to B\r\n");
		return;         // 已确认，不用回滚
	}
	// 未确认
	uart_print(other_flag == BOOT_TO_B ? "A unconfirmed! Rollback\r\n" : "B unconfirmed! Rollback\r\n");
	
	if (is_app_bootable(other_addr)) {
		Flash_unlock();
		Flash_erase_page(FLAG_ADDR);
		Flash_write_halfword(FLAG_ADDR,    other_flag & 0xFFFF);
		Flash_write_halfword(FLAG_ADDR+2, (other_flag >> 16) & 0xFFFF);
		Flash_lock();
		BKP->DR4 = 0;
		uart_print(other_flag == BOOT_TO_B ? "Back to B\r\n" : "Back to A\r\n");
		jump_to_app_safe(other_addr);
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
		g_fw_size = (uint32_t)BKP->DR2 | ((uint32_t)BKP->DR3 << 16);	
      	uart_print("OTA request\r\n");
      	BKP->DR1 = 0;               // 清标记
      	ota_update();               // 读 W25Q64 → 写 Flash → 跳转
		// ota_update 里会跳转，不会回到这
  	}

	// 读标志区	
	flag = *(volatile uint32_t *)FLAG_ADDR;
	if (flag == BOOT_TO_A) {
		// 回滚检查：如果 DR4 != 0x0000，说明新固件没确认过
		check_rollback(APP_B_ADDR, BOOT_TO_B);
		jump_to_app_safe(APP_A_ADDR);
	} 
	else if (flag == BOOT_TO_B) {
		check_rollback(APP_A_ADDR, BOOT_TO_A);
		jump_to_app_safe(APP_B_ADDR);
	}
	
	uart_print("Fallback: try A\r\n");
	// CRC 不对，但可能是 ST-Link 烧的，做一次弱校验
	if (is_app_bootable(APP_A_ADDR)) {
		uart_print("ST-Link app, write flag\r\n");
		// 第一次跑，写标记
		Flash_unlock();
		Flash_erase_page(FLAG_ADDR);
		Flash_write_halfword(FLAG_ADDR,    BOOT_TO_A & 0xFFFF);
		Flash_write_halfword(FLAG_ADDR+2, (BOOT_TO_A >> 16) & 0xFFFF);
		Flash_lock();
		uart_print("Boot to A");
		jump_to_app_safe(APP_A_ADDR);
	}

	while (1) {
		led_on();
		for (volatile uint32_t i = 0; i < 2000000; i++);
		led_off();
		for (volatile uint32_t i = 0; i < 2000000; i++);
	}
}


