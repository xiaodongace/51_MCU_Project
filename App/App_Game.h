/*
 * App_Game.h - 游戏掌机模式的占位（M2 才实现）
 *
 * 为什么要现在就建这个文件（用户明确要求"给游戏掌机留出位置"）：
 *   1) 页面编号已经在 App_Public.h 的 UIPageId_t 里占好了
 *      （PAGE_GAME_HALL / SNAKE / BRICK / PLANE / DAILY / OVER）；
 *   2) 显示层已经认识这些页面，主屏会画出"掌机模式 M2 开发中"的占位页；
 *   3) EEPROM 的 1 号区一直空着没被 M1 写过，留给游戏存档；
 *   4) Timer4 一个中断都没注册，留给游戏的弹幕节奏。
 *
 * M2 要做的只是把下面几个函数填上，页面框架、存储分区、定时器都不用改。
 */
#ifndef __APP_GAME_H
#define __APP_GAME_H

#include "App_Public.h"

/* 上电初始化（M1 只清状态） */
void Game_Init(void);

/*
 * 请求进入掌机模式。
 * 返回 1 = 进入成功（外层会把 PAGE_GAME_HALL 压入页面栈）；
 * 返回 0 = 暂不支持，外层什么都不做。
 */
u8 Game_Enter(void);

/* 掌机页面的按键处理（M1 只打印，不改变任何东西） */
void Game_OnEvent(const Event_t *evt);

/* 掌机页面的秒级逻辑（M1 空实现） */
void Game_Tick1s(void);

/* 是不是掌机模式下的页面（显示层用它决定要不要画占位页） */
u8 Game_IsGamePage(UIPageId_t page);

#endif
