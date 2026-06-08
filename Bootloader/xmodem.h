#ifndef XMODEM_H
#define XMODEM_H

#include "stm32f10x.h"

#ifndef NULL
	#define NULL ((void *)0)
#endif

// XMODEM 控制字符
#define XMODEM_SOH     0x01
#define XMODEM_STX     0x02
#define XMODEM_EOT     0x04
#define XMODEM_ACK     0x06
#define XMODEM_NAK     0x15
#define XMODEM_CAN     0x18
#define XMODEM_CRC     0x43    // 'C'

#define XMODEM_DATA_SIZE  128

// 接收状态
typedef enum {
  XMODEM_OK = 0,
  XMODEM_ERR_CANCEL,
  XMODEM_ERR_CRC,
  XMODEM_ERR_SEQ,
  XMODEM_ERR_TIMEOUT
} XmodemStatus;


// 接收整个文件到指定的 Flash 地址
uint32_t  xmodem_receive(uint32_t flash_addr,uint32_t *total_size);

#endif
