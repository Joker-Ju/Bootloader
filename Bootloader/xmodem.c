#include "xmodem.h"
#include "Flash.h"

// 串口字节发送
static void uart_putchar(uint8_t c) {
	while (!(USART1->SR & USART_SR_TXE));
	USART1->DR = c;
}

// 串口字节接收（带超时）
// 返回值：0=超时，1=成功读到
static uint8_t uart_getchar(uint8_t *c, uint32_t timeout)
{
	while (timeout--) {
		if (USART1->SR & USART_SR_RXNE) {   // 有数据到了
			*c = USART1->DR;
			return 1;
		}
		// 简单延时：72MHz 下大约每次循环几个微秒
		for (volatile uint32_t i = 0; i < 100; i++);
	}
	return 0;   // 超时
}

// CRC16
uint16_t xmodem_crc16(const uint8_t *data, uint32_t len) {
	uint16_t crc = 0;
	for (uint32_t i = 0; i < len; i++) 
	{
		crc ^= (uint16_t)data[i] << 8;
		for (int j = 0; j < 8; j++) 
		{
			if (crc & 0x8000)
				crc = (crc << 1) ^ 0x1021;
			else
				crc <<= 1;
		}
	}
	return crc;
}

uint32_t xmodem_receive(uint32_t flash_addr, uint32_t *total_size)
{
	uint8_t  packet[128];       // 数据缓冲区（一包 128 字节）
	uint8_t  seq = 1;           // 期望的包编号（从 1 开始）
	uint32_t offset = 0;        // 已写入的字节数
	uint8_t  byte;
	uint16_t crc, calc;

	// ── 不停发 'C'，直到收到 PC 响应 ──
	uart_putchar(XMODEM_CRC);
	while (1) {
		if (uart_getchar(&byte, 50000)) {
			break;              // 收到 PC 的回应了
		}
		uart_putchar(XMODEM_CRC);  // 超时，再发一次 'C'
	}
	while (1) {
		// ── 根据收到的第一个字节判断 ──
		if (byte == XMODEM_SOH) {
			// ── 这是一个 128 字节数据包 ──

			// 1. 读编号
			if (!uart_getchar(&byte, 1000)) return 1;
			uint8_t num = byte;

			// 2. 读反码
			if (!uart_getchar(&byte, 1000)) return 1;
			uint8_t inv = byte;

			// 3. 检查编号有效性：编号 + 反码 必须 = 0xFF
			if ((num + inv) != 0xFF) {
				uart_putchar(XMODEM_NAK);
				goto next_byte;
			}

			// 4. 读 128 字节数据
			for (int i = 0; i < 128; i++) {
				if (!uart_getchar(&packet[i], 1000)) return 1;
			}

			// 5. 读 2 字节 CRC
			if (!uart_getchar(&byte, 1000)) return 1;
			crc = (uint16_t)byte << 8;          // CRC 高字节
			if (!uart_getchar(&byte, 1000)) return 1;
			crc |= byte;                        // 拼上 CRC 低字节

			// 6. 本机算 CRC
			calc = xmodem_crc16(packet, 128);

			// 7. ── 判断编号 ──
			if (num == seq) {
				// 这是我们等的包
				if (calc == crc) {
					// CRC 正确！写 Flash，回 ACK
					Flash_write_data(flash_addr + offset, packet, 128);
					offset += 128;
					uart_putchar(XMODEM_ACK);
					seq++;                      // 期待下一个编号
				} else {
					// CRC 错误，回 NAK 让 PC 重发
					uart_putchar(XMODEM_NAK);
				}
			}
			else if (num == seq - 1) {
				// PC 重发了上一包（ACK 丢了）
				// 回 ACK，但不写 Flash（因为已经写过了）
				uart_putchar(XMODEM_ACK);
			}
			else {
				// 编号完全对不上，协议错误
				return 1;
			}
		}
		else if (byte == XMODEM_EOT) {
			// ── 传输结束 ──
			uart_putchar(XMODEM_ACK);
			if (total_size) *total_size = offset;   // 告诉调用者收了多大
			return 0;   // 成功！
		}
		else if (byte == XMODEM_CAN) {
			// ── PC 取消了传输 ──
			return 1;
		}
		else {
			// ── 未知字节，回 NAK ──
			uart_putchar(XMODEM_NAK);
		}
next_byte:
		// 等下一个字节
		if (!uart_getchar(&byte, 10000)) {
			return 1;   // 超时太长了，放弃
		}
	}
}


//uint32_t xmodem_receive(uint32_t flash_addr, uint32_t *total_size)
//{
//	uint8_t byte;

//		// 发 'C'
//		uart_putchar(XMODEM_CRC);

//  while (1) {
//		if (uart_getchar(&byte, 50000)) {
//			// 收到什么，就发回什么（回环测试）
//			uart_putchar(byte);
//		} else {
//			uart_putchar(XMODEM_CRC);   // 超时就发 'C'
//		}
//	}
//}

//uint32_t xmodem_receive(uint32_t flash_addr, uint32_t *total_size)
//{
//  while (1) {
//	  uart_putchar('U');           // 不停发 U
//	  for (volatile uint32_t i = 0; i < 1000000; i++);  // 延时
//  }
//}
 
