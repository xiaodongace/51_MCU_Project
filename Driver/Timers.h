/*
 * Timers.h - 全项目统一的 1ms 系统时钟（"墙上的钟"，Timer3）
 *
 * 这是《02-技术方案》4.1 讲的那个"屋子里大家共用的一只钟"：
 * Timer3 每 1 毫秒把 g_sysTick 拨一格，所有需要计时的地方都用
 * SysTick_Get() / SysTick_Elapsed() 读差值，不靠 os_wait2 站着等。
 *
 * ※ 本文件是 2026-09-17 真机联调第 2 轮补上的。
 *   第 1 版以 day17 v3.1 为基座搬库时漏了这个文件的初始化：
 *   Lib/Timer_Isr.c 里的 Timer3_ISR_Handler 已经把 g_sysTick++ 写好了，
 *   但全工程没有任何地方调用 Timer_Inilize(Timer3,...) 与 NVIC_Timer3_Init(...)，
 *   于是 Timer3 从来没启动过、中断从来没产生过，g_sysTick 恒为 0。
 *
 *   后果（一次解释一串现象）：
 *     · Keys_Scan() 的消抖判据 (now - t0) >= 20ms 永远不成立 -> 按键永不产生事件
 *     · TASK_LOGIC 的 1 秒节拍 >= 1000ms 永远不成立 -> 读时钟/闹钟判断/日出唤醒全不执行
 *       （副屏时间冻结在开机那一刻，就是这个原因）
 *     · Music_Tick() 的音符时长判断永远不成立 -> 曲子永远停在第一个音
 */
#ifndef __TIMERS_H
#define __TIMERS_H

#include "Config.h"

/* 初始化 Timer3：16 位自动重装、1T 时钟源、1kHz 中断，并打开 Timer3 中断。
 * 必须在 EA = 1 之前调用（在 sys_init 里）。 */
void Timers_Init(void);

/* 读取系统毫秒计数（和 App_Public.c 的 SysTick_Get 等价，保留这个名字是为了
 * 与用户现有工程的叫法一致，两边都能用） */
u32 Timers_GetSystemMs(void);

#endif
