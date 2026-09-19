/*
 * App_Uart.h - 上位机通信协议（串口1，P3.0/P3.1 经 CH340 到 USB）
 *
 * 帧格式（主机与板子双向一致）：
 *
 *   +------+------+------+------+-----------+------+
 *   | 0xAA | 0x55 | LEN  | CMD  | DATA[..]  | SUM  |
 *   +------+------+------+------+-----------+------+
 *
 *   LEN = CMD + DATA 的字节数（不含帧头与校验）
 *   SUM = (LEN + CMD + DATA 各字节) 之和的低 8 位
 *
 * 为什么这样定：定长头 + 长度字段 + 校验，主机侧用 Python 拼字节串最省事，
 * 也不依赖任何"文本行"约定，避免 printf 的调试信息把二进制帧搅乱。
 *
 * 分工（这是本工程的一条硬规矩）：
 *   串口接收中断只负责把字节塞进 Lib 的 RX1_Buffer；
 *   本模块只在 TASK_LOGIC 里被调用，做解析与执行；
 *   App 层其它模块不碰串口。
 */
#ifndef __APP_UART_H
#define __APP_UART_H

#include "App_Public.h"

/* 命令字 */
#define UC_GET_TIME         0x01
#define UC_SET_TIME         0x02
#define UC_GET_SETTINGS     0x03
#define UC_SET_SETTINGS     0x04
#define UC_GET_ALARMS       0x05
#define UC_SET_ALARM        0x06
#define UC_DEL_ALARM        0x07
#define UC_GET_ENV          0x08
#define UC_GET_LOGCOUNT     0x09
#define UC_GET_LOG          0x0A
#define UC_GET_VERSION      0x0B

/* 回复状态 */
#define UC_OK               0x00
#define UC_FAIL             0x01

/* 复位解析状态机（上电调用一次） */
void Uart_Init(void);

/*
 * 从接收缓冲取字节喂给解析器。由 TASK_LOGIC 每 10ms 调用一次。
 * 一帧收齐后立即执行并回复。
 */
void Uart_Poll(void);

/* 已解析并执行的帧数（调试用） */
u16 Uart_FrameCount(void);

/* 校验失败 / 帧错误的次数（调试用） */
u16 Uart_ErrorCount(void);

#endif
