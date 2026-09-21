#ifndef __KEY_H
#define __KEY_H

#include "App_Public.h"
#include "Timers.h"

/*
 * 初始化四个独立按键和消抖状态
 */
void Key_Init(void);

/*
 * 周期扫描四个按键
 * 这个函数只能由菜单任务调用
 */
void Key_Scan(void);

/*
 * 获取按下事件
 * 按键完成消抖并确认按下时产生一次
 */
u8 Key_GetPressEvent(u8 key_index);

/*
 * 获取短按事件
 * 按下时间小于长按阈值，并且松开时产生一次
 */
u8 Key_GetShortPressEvent(u8 key_index);

/*
 * 获取长按事件
 * 按住达到指定时间时产生一次
 */
u8 Key_GetLongPressEvent(u8 key_index);

/*
 * 查询按键当前物理电平是否为按下
 */
u8 Key_IsPressed(u8 key_index);

#endif