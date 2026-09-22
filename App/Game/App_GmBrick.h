#ifndef __APP_GM_BRICK_H__
#define __APP_GM_BRICK_H__

/*------------------------------------------------------------------------
 *  【移植说明 · 2026-09-22】
 *  本文件移植自参考工程《23_基于stc8的多功能时钟》的 User/App_GmBrick.h。
 *  移植时做的改动（全部是机械适配，游戏逻辑一行没动）：
 *    1. #include "oled.h"  ->  "SPI_OLED.h"
 *       参考工程画在副屏（I2C）；本项目按用户要求**画在主屏（SPI）**，
 *       而两个驱动的这几个函数**同名同参数**：
 *           OLED_Display_GB2312_string <-> SPI_OLED_Display_GB2312_string
 *           OLED_DrawPoint             <-> SPI_OLED_DrawPoint
 *           OLED_GClear                <-> SPI_OLED_GClear
 *           OLED_Refresh               <-> SPI_OLED_Refresh
 *       所以只要把前缀换掉，绘制就整体搬到主屏上了。
 *    2. Delay_ms -> delay_ms
 *    3. Game_* / 全局名加 Brick_ 前缀（本工程 App_Game.c 已占用 Brick_Init）
 *------------------------------------------------------------------------*/

#include "GPIO.h"
#include "SPI_OLED.h"

/* 游戏状态定义 */
#define GAME_RUNNING 0
#define GAME_PAUSED  1
#define GAME_OVER    2

/* 挡板结构 */
typedef struct {
    u8 x;     /* 挡板中心x坐标 */
    u8 width; /* 挡板宽度 */
} Paddle;

/* 球结构 */
typedef struct {
    u8 x, y;     /* 球的位置 */
    /* 【2026-09-22 修正】原来是 char —— Keil C51 的 char 默认是**无符号**，
     * 存 -1 会变成 255。虽然加减法靠模运算"碰巧"还对，但可读性差、
     * 也容易在比较时踩坑（dx > 0 这种判断语义就变了）。改成显式 s8。 */
    s8 dx, dy;   /* 球的速度方向（-1 / +1）*/
} Ball;

/* 砖块结构 */
typedef struct {
    u8 x, y;    /* 砖块位置 */
    u8 width;   /* 砖块宽度 */
    u8 height;  /* 砖块高度 */
    u8 visible; /* 是否可见 (0或1) */
} Brick;

/* 游戏全局变量声明 */
extern u8 brickState;
extern Paddle brickPaddle;
extern Ball brickBall;
extern Brick brickList[20];
extern u8 brickScore;
extern u8 brickLives;

/* 函数声明 */
void Brick_Init(void);
void Brick_Draw(void);
void Brick_Update(void);
void Brick_MovePaddle(char direction);
void Brick_ShowGameOver(void);

#endif /* __APP_GM_BRICK_H__ */