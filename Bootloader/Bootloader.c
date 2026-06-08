#include "Flash.h"     // Flash 操作
#include "xmodem.h"    // XMODEM 协议


// ── 地址定义 ──
#define FLAG_ADDR       0x0800E000
#define BOOT_TO_A       0xAAAAAAAA
#define BOOT_TO_B       0xBBBBBBBB
#define UPGRADE_REQ     0x55AA55AA

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

// ── GPIO 初始化（PC13=LED，PA0=升级触发） ──
void gpio_init(void) {
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN;

	// PC13: 推挽输出（LED）
	GPIOC->CRH &= ~(0xF << 20);           // 清除 CNF13、MODE13
	GPIOC->CRH |= (0x3 << 20);            // MODE13=11, CNF13=00 → 推挽输出 50MHz

	// PA0: 上拉输入（检测低电平触发升级）
	GPIOA->CRL &= ~(0xF << 0);            // 清除 CNF0、MODE0
	GPIOA->CRL |= (0x8 << 0);             // CNF0=10, MODE0=00 → 上拉/下拉输入
	GPIOA->ODR |= (1 << 0);               // PA0 上拉，默认高电平
}

void led_on(void)  { GPIOC->ODR &= ~(1 << 13); }  // PC13=0 → LED 亮
void led_off(void) { GPIOC->ODR |= (1 << 13); }   // PC13=1 → LED 灭

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


// ── 升级模式 ──
void upgrade_to(uint32_t target_addr)
{

	// 1. 擦除 App 分区
	Flash_unlock();
	Flash_erase_app_area(target_addr, 24 * 1024);
	uint32_t ret = xmodem_receive(target_addr, 0);

	if (ret == 0) {  // 传输成功！
		// 写标记
		Flash_erase_page(FLAG_ADDR);
		uint32_t new_flag = (target_addr == APP_B_ADDR) ? BOOT_TO_B : BOOT_TO_A;
		Flash_write_halfword(FLAG_ADDR,    new_flag & 0xFFFF);
		Flash_write_halfword(FLAG_ADDR+2, (new_flag >> 16) & 0xFFFF);
		Flash_lock();
		led_off();
		// ── 设 BKP 未确认标记（App 启动成功后会改成 0x0000） ──
		RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
		PWR->CR |= PWR_CR_DBP;
		BKP->DR4 = 0xFFFF;        // 未确认
		jump_to_app(target_addr);
	} else {
		// 传输失败，重新锁回去，等下次复位再试
		Flash_lock();
		// LED 继续亮着，人看到就知道坏了
	}
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
	// 读 BKP 不需要 DBP，只要时钟使能就能读
	if (BKP->DR1 == 0x5555) {
		uint32_t target = ((uint32_t)BKP->DR2 << 16) | BKP->DR3;
		const char *from = (target == APP_B_ADDR) ? "A" : "B";  // 目标不是 B → 当前就是 B
		const char *to   = (target == APP_B_ADDR) ? "B" : "A";
		uart_print("Current: ");
		uart_print(from);
		uart_print(", Upgrade to: ");
		uart_print(to);
		uart_print("\r\n");
		BKP->DR1 = 0;  // 清标记
		upgrade_to(target);  // 写到目标分区
		// upgrade_to 里会写 Flash 标志 + 跳转，不会返回到这里
	}
	
	
	// 读标志区
	flag = *(volatile uint32_t *)FLAG_ADDR;

	if (flag == UPGRADE_REQ) {
		uart_print("Flag: UPGRADE_REQ\r\n");
		upgrade_to(APP_A_ADDR);  // 这里可以改进成读 FLAG_ADDR+4 的目标地址
	  // 不会再回到这里
	}

	if (flag == BOOT_TO_A) {
		// 回滚检查：如果 DR4 != 0x0000，说明新固件没确认过
		if (BKP->DR4 != 0x0000) {
			uart_print("A unconfirmed! Rollback to B\r\n");
			Flash_unlock();
			Flash_erase_page(FLAG_ADDR);
			Flash_write_halfword(FLAG_ADDR,    BOOT_TO_B & 0xFFFF);
			Flash_write_halfword(FLAG_ADDR+2, (BOOT_TO_B >> 16) & 0xFFFF);
			Flash_lock();
			BKP->DR4 = 0;
			// 切到 B
			if (is_app_valid(APP_B_ADDR)) {
				uart_print("Back to B\r\n");
				jump_to_app(APP_B_ADDR);
			}
		}
		uart_print("Boot to A\r\n");
		if (is_app_valid(APP_A_ADDR)) {
			jump_to_app(APP_A_ADDR);
		}
	} 
	else if (flag == BOOT_TO_B) {
		if (BKP->DR4 != 0x0000) {
			uart_print("B unconfirmed! Rollback to A\r\n");
			Flash_unlock();
			Flash_erase_page(FLAG_ADDR);
			Flash_write_halfword(FLAG_ADDR,    BOOT_TO_A & 0xFFFF);
			Flash_write_halfword(FLAG_ADDR+2, (BOOT_TO_A >> 16) & 0xFFFF);
			Flash_lock();
			BKP->DR4 = 0;
			if (is_app_valid(APP_A_ADDR)) {
				uart_print("Back to A\r\n");
				jump_to_app(APP_A_ADDR);
			}	
		}
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

	uart_print("No valid app, wait for XMODEM...\r\n");
	upgrade_to(APP_A_ADDR);
	while (1) {
		
	  // 在这里初始化 USART，等固件…
	}
}


