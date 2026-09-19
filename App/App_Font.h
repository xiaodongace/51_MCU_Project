/*
 * App_Font.h - 16x32 大字数字字模索引定义
 *
 * 布局与字模说明见 App_Font.c 头部注释。
 * 本字模用于主屏显示"几点几分"的大字时间。
 */
#ifndef __APP_FONT_H
#define __APP_FONT_H

#include "Config.h"

/* 字模索引 */
#define FONT_0      0
#define FONT_1      1
#define FONT_2      2
#define FONT_3      3
#define FONT_4      4
#define FONT_5      5
#define FONT_6      6
#define FONT_7      7
#define FONT_8      8
#define FONT_9      9
#define FONT_COLON  10
#define FONT_MINUS  11
#define FONT_BLANK  12

/* 每个字模字节数 = 16 列 x 32 行 / 8 = 64 */
#define FONT_BYTES  64

/* 单字模宽度（像素列） */
#define FONT_WIDTH  16

/* 大字高度对应的页数 = 32 / 8 = 4 */
#define FONT_PAGES  4

extern u8 code DIGIT32[13][FONT_BYTES];

#endif
