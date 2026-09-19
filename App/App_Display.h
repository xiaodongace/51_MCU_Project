/*
 * App_Display.h - 两块屏幕的绘制（TASK_RENDER）
 *
 * 屏幕分工（《01-项目介绍》第四节）：
 *   主屏（SPI，带硬件字库）：大字时间 + 日期 + 温湿度。**能显示汉字**（字库在屏幕模块上）。
 *   副屏（I2C，无字库）：只用 ASCII/数字。**不能显示汉字**（Hzk 表里只有 7 个固定字）。
 *
 * 坐标约定（规范第一节要求从驱动源码确认，不能凭记忆）：
 *   两块屏都是 x = 像素列 0..127，y = **页** 0..7（1 页 = 8 像素行）——
 *   依据：SPI_OLED_address() 里写的是 0xb0+y，I2C_OLED_Set_Pos() 也一样。
 *   16x16 汉字占 2 页高；32 像素高的大字占 4 页。
 *
 * 刷新策略（2026-09-19 UI 改造后）：
 *   主屏（SPI，约 1ms 一次，很便宜）：不做整屏重绘，按"哪一行真的变了"分别重画 ——
 *     大字时间：分钟变了才重画；日期行：日期变了才重画；温湿度行：变了或每 2 秒一次。
 *   副屏（I2C，整屏约 350ms，很贵）：**没有任何周期性写屏**，
 *     只在"内容形态变化"时整块重画 —— 切菜单 / 换任务 / 云台转针。
 *     副屏显示什么由层级决定：任务清单→图标；云台→仪表盘；其余（含主菜单）→全黑。
 */
#ifndef __APP_DISPLAY_H
#define __APP_DISPLAY_H

#include "App_Public.h"

/* 初始化两块屏并画第一帧 */
void Display_Init(void);

/* 请求下一拍做一次全屏重绘（切页面、从响铃页返回时用） */
void Display_RequestFull(void);

/* 只请求重画主屏（SPI）。
 * 用于"只影响主屏"的变化（例如任务清单挪光标）—— 它**不受 I2C 的整屏限流约束**，
 * 因为主屏是软件 SPI，画一次约 1ms，根本没有限流的必要。 */
void Display_RequestMain(void);

/* 只请求重画云台仪表盘（半圆 + 粗针 + 角度行） */
void Display_RequestGauge(void);

/* 只请求重画任务清单的图标（64x48，384 字节） */
void Display_RequestIcon(void);

/* 当前页面编号发生变化时通知显示层（用于副屏标题） */
void Display_SetPage(UIPageId_t page);

/* 由 TASK_RENDER 调用一次，内部自己判断该重画什么 */
void Display_Poll(void);

/* 主屏开关（设置页可以关屏省电） */
void Display_MainPower(u8 on);

#endif
