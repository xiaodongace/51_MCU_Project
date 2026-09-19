/*
 * DHT11.h - 温湿度传感器（P4.6，单总线）
 *
 * 真值来源：
 *   v3.1 Driver/DHT11.c  -> 完整时序（拉低 20ms、等释放、响应低/高电平、40 位数据、校验）
 *   江文聪封装驱动 DHT11.c -> P4.6 准双向口（GPIO_PullUp），"不是推挽"
 *   《04-M1分工规格》F 角色 -> 参考代码用小数运算要改成整数；delay_us 要加 static
 *
 * 相对 v3.1 的改动（按规范显式列出）：
 *   1) 返回值由 float 改成整数：温度以"x10"为单位（253 表示 25.3℃）。
 *      理由：只要算式里出现 float，C51 就会链入整个 C51FPL.LIB（现有工程实测已链入
 *      ?C?FPMUL / ?C?FPDIV / ?C?FPADD / ?C?FPCONVERT / PRINTF）。
 *   2) 读那 40 位数据期间关中断（EA=0），见 DHT11.c 注释。
 */
#ifndef __DHT11_H
#define __DHT11_H

#include "Config.h"

#define DHT             P46

/* 数据线用准双向口（江文聪封装驱动原文：// P46 准双向口） */
#define DHT11_GPIO_INIT()   P4_MODE_IO_PU(GPIO_Pin_6)

/* 读取失败的错误码（负数） */
#define DHT11_ERR_RELEASE   (-3)    /* 等主机释放总线超时 */
#define DHT11_ERR_RESP_LOW  (-4)    /* 响应低电平时间不对 */
#define DHT11_ERR_RESP_HIGH (-5)    /* 响应高电平时间不对 */
#define DHT11_ERR_DATA_LOW  (-6)    /* 数据位低电平时间不对 */
#define DHT11_ERR_DATA_HIGH (-7)    /* 数据位高电平时间不对 */
#define DHT11_ERR_CHECKSUM  (-8)    /* 校验和不匹配 */

/* 初始化：配 P4.6 为准双向口 */
void DHT11_Init(void);

/*
 * 读一次温湿度。
 *   tempX10 : 温度 x10，如 253 表示 25.3℃；负数表示零下温度
 *   humi    : 相对湿度整数部分，0..100
 * 返回 0 表示成功，负数为错误码（见上）。
 *
 * 注意：本函数会关中断约 4.6ms（见 DHT11.c 说明），由 TASK_SENSOR 每秒调用一次。
 */
u8 DHT11_Read(s16 *tempX10, u8 *humi);

#endif
