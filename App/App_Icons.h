/*
 * App_Icons.h - 主屏任务清单用的图标（64x48，画在 I2C 副屏上）
 *
 * 用途：在第 1 级任务清单里，光标指到哪个任务，I2C 就显示哪个任务的图。
 *       版面 A —— 整屏只有一张图，不放文字（任务名在主屏菜单上已经有了）。
 */
#ifndef __APP_ICONS_H
#define __APP_ICONS_H

#include "Config.h"

/* 取第 idx 张图标，返回指向 code 区的指针（384 字节，页格式） */
u8 code *Icon_Get(u8 idx);

#endif
