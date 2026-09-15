#ifndef __KEY_H
#define __KEY_H

#include "App_Public.h"
#include "Timers.h"

/*
 * 初始化按键GPIO及公共非阻塞消抖状态
 */
void Key_Init(void);


/*
 * 扫描所有按键并用系统时间执行非阻塞消抖
 * 函数只更新按键状态并产生事件
 */
void Key_Scan(void);


/*
 * 读取并消费指定按键的一次性按下事件
 * 返回1表示取得一个新事件
 * 事件读取后立即清零
 * 按住按键不会重复触发
 */
u8 Key_GetPressEvent(u8 key_index);


/*
 * 查询按键当前是否被按下
 * 非零为按下 0 为松开
 */
u8 Key_IsPressed(u8 key_index);


#endif
