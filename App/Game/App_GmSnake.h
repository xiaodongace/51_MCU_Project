#ifndef __SNAKE_H__
#define __SNAKE_H__

/*------------------------------------------------------------------------
 *  【移植说明 · 2026-09-22】
 *  本文件移植自参考工程《23_基于stc8的多功能时钟》的 User/App_GmSnake.h。
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
 *    3. 游戏内的全局名一律加自己的前缀（Snake），
 *       避免和主工程同名 —— 早期 App_Game.c 占用了 Game_* 这组名字，
 *       那个文件现已废弃清空，但前缀保持不变，免得改动已验证的游戏代码。
 *------------------------------------------------------------------------*/

#include "GPIO.h"
#include "SPI_OLED.h"



/* 贪吃蛇游戏状态 */
#define SNAKE_STATE_MENU 0
#define SNAKE_STATE_PLAYING 1
#define SNAKE_STATE_GAME_OVER 2
#define SNAKE_STATE_PAUSED 3

/* 方向定义 */
#define SNAKE_DIR_UP 0
#define SNAKE_DIR_RIGHT 1
#define SNAKE_DIR_DOWN 2
#define SNAKE_DIR_LEFT 3

/* 蛇身节点结构 */
typedef struct SnakeNode
{
    u8 x;
    u8 y;
    struct SnakeNode* next;
} SnakeNode;

/* 游戏全局变量 */
extern u8 snakeGameState;
extern u8 snakeDirection;
extern u8 foodX;
extern u8 foodY;
extern u8 snakeScore;
extern u8 snakeHighScore;
extern SnakeNode* snakeHead;

/* 函数声明 */
void Snake_Init(void);
void Snake_Update(void);
void Snake_Draw(void);
void Snake_HandleInput(u8 direction);
void Snake_GenerateFood(void);
u8 Snake_CheckCollision(u8 x, u8 y);
void Snake_ShowGameOver(void);

#endif /* __SNAKE_H__ */