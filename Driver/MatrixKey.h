/*
 * MatrixKey.h - 4x4 矩阵键盘（16 键）
 *
 * 真值来源：v3.1 Driver/MatrixKey.h / .c（真机跑通的扫描方法，逐字对齐引脚）
 *   列 COL1..COL4 = P0.3 / P0.6 / P0.7 / P1.7
 *   行 ROW1..ROW4 = P3.4 / P3.5 / P4.0 / P4.1
 *   全部准双向口；扫描时逐行拉低，读列电平；按下=0，抬起=1
 *
 * 相对 v3.1 的改动（按规范显式列出）：
 *   1) 加上 20ms 非阻塞消抖（《04》E2 要求"不串键"，无消抖时抖动会误报）；
 *   2) 扫完把 4 行恢复为高电平（《04》E 角色注意事项："扫完一定要恢复原状"）。
 */
#ifndef __MATRIXKEY_H
#define __MATRIXKEY_H

#include "Config.h"

/* 列 column */
#define COL1    P03
#define COL2    P06
#define COL3    P07
#define COL4    P17

/* 行 row */
#define ROW1    P34
#define ROW2    P35
#define ROW3    P40
#define ROW4    P41

#define MK_ROW_NUM      4
#define MK_COL_NUM      4
#define MK_KEY_COUNT    (MK_ROW_NUM * MK_COL_NUM)

#define MK_DEBOUNCE_MS  20

#define MK_GPIO_INIT()                                       \
    P0_MODE_IO_PU(GPIO_Pin_3 | GPIO_Pin_6 | GPIO_Pin_7);     \
    P1_MODE_IO_PU(GPIO_Pin_7);                               \
    P3_MODE_IO_PU(GPIO_Pin_4 | GPIO_Pin_5);                  \
    P4_MODE_IO_PU(GPIO_Pin_0 | GPIO_Pin_1);

/* 初始化 */
void MK_Init(void);

/* 扫描：由 TASK_INPUT 每 10ms 调用一次。非阻塞。 */
void MK_Scan(void);

/*
 * 以下两个回调由 App 层（App_Input.c）实现。
 * row / col 均取 0..3，按键编号 = row * 4 + col。
 */
extern void MK_on_keydown(u8 row, u8 col);
extern void MK_on_keyup(u8 row, u8 col);

#endif
