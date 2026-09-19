/*
 * App_Input.h - 输入层：把按键、矩阵键盘、旋钮统一变成"事件"
 *
 * 设计（对齐《04-M1分工规格》E 角色 E4）：
 *   Driver 层的按键扫描只负责"哪个键、按下还是抬起"，通过回调交给本模块；
 *   本模块把它们打包成 Event_t 放进队列；
 *   App 层（菜单、闹钟）只从队列里取事件，完全不需要知道按键接在哪个引脚上。
 */
#ifndef __APP_INPUT_H
#define __APP_INPUT_H

#include "App_Public.h"

/* 事件队列深度。人手工按键最快约 10 次/秒，TASK_INPUT 每 10ms 排空一次，
 * 16 格余量足够（《04》E1 要求"连按 100 次不丢键"）。 */
#define EVT_QUEUE_SIZE  16

/* 初始化：按键、矩阵键盘、旋钮 */
void Input_Init(void);

/* 由 TASK_INPUT 每 10ms 调用一次 */
void Input_Scan10ms(void);

/* 取一条事件。返回 1 表示取到，0 表示队列为空 */
u8 Input_GetEvent(Event_t *evt);

/* 队列里当前待处理的事件条数（调试用） */
u8 Input_PendingCount(void);

/* 清空队列（切页面时用，避免旧事件串到新页面） */
void Input_Flush(void);

/* 旋钮当前档位 0..10 */
u8 Input_GetPotLevel(void);

#endif
