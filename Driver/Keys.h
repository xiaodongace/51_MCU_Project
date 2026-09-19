/*
 * Keys.h - 4 个独立按键（P5.1 ~ P5.4）
 *
 * 真值来源：
 *   v3.1 Driver/Keys.h  -> 引脚 KEY1=P51 … KEY4=P54，P5_MODE_IO_PU 准双向口
 *   v3.1 Driver/Keys.c  -> 按下=0（低），抬起=1（高）
 *   《04-M1分工规格》E 角色 -> 防抖 20ms；短按/长按区分，长按 2 秒且只触发一次
 *
 * 相对 v3.1 的改动（按规范显式列出）：
 *   v3.1 的 Keys_scan() 只做边沿判断，没有任何消抖，手一抖就是一次"按下"。
 *   本文件按《04》E1 加上 20ms 非阻塞消抖 + 2 秒长按，消抖计时用 g_sysTick，
 *   不用 delay 硬等（《04》E 角色注意事项）。
 */
#ifndef __KEYS_H
#define __KEYS_H

#include "Config.h"

#define KEY1    P51
#define KEY2    P52
#define KEY3    P53
#define KEY4    P54

#define KEY_COUNT       4

/* 电平语义：按下拉低（照抄 v3.1 Driver/Keys.c 与江文聪 Keys.c 的注释） */
#define KEY_LEVEL_DOWN  0
#define KEY_LEVEL_UP    1

/* 消抖时间：10ms -> 20ms（《04》E1 建议值） */
#define KEY_DEBOUNCE_MS     20

/* 长按判定时间：2 秒（《04》E 角色） */
#define KEY_LONGPRESS_MS    2000

#define KEYS_GPIO_INIT()    P5_MODE_IO_PU(GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4)

/* 初始化：配 IO + 用当前真实电平初始化消抖状态（避免上电把电平当成一次按下） */
void Keys_Init(void);

/* 扫描：由 TASK_INPUT 每 10ms 调用一次。非阻塞。 */
void Keys_Scan(void);

/* 查询某键当前是否按下（消抖后的稳定状态），key = 0..3 */
u8 Keys_IsPressed(u8 key);

/*
 * 以下三个回调由 App 层（App_Input.c）实现。
 * key 取 0..3，分别对应 KEY1..KEY4。
 * 注意：v3.1 传的就是 0..3（江文聪老师的封装驱动传 1..4），本工程统一用 0..3。
 */
extern void Keys_on_keydown(u8 key);
extern void Keys_on_keyup(u8 key);
extern void Keys_on_longpress(u8 key);

#endif
