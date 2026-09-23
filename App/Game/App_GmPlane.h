#ifndef __PLANE_GAME_H__
#define __PLANE_GAME_H__

/*------------------------------------------------------------------------
 *  【移植说明 · 2026-09-22】
 *  本文件移植自参考工程《23_基于stc8的多功能时钟》的 User/App_GmPlane.h。
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
 *    3. 游戏内的全局名一律加自己的前缀（PlaneGame），
 *       避免和主工程同名 —— 早期 App_Game.c 占用了 Game_* 这组名字，
 *       那个文件现已废弃清空，但前缀保持不变，免得改动已验证的游戏代码。
 *------------------------------------------------------------------------*/

#include "GPIO.h"
#include "SPI_OLED.h"

/* 子弹槽个数。
 * 【2026-09-22 用户要求】"每 3 秒只能发射一次子弹，一次子弹为 10 枚"，
 * 所以从 5 个扩到 10 个 —— 一次齐射正好把槽占满。
 * 子弹飞完全屏只要约 0.4 秒，而冷却有 3 秒，所以"槽被上一轮占着"
 * 这种情况实际不会发生。 */
#define PLANE_BULLET_MAX 10

/* 游戏状态 */
#define PLANE_STATE_MENU 0
#define PLANE_STATE_PLAYING 1
#define PLANE_STATE_GAME_OVER 2

/* 按键定义 */
#define PLANE_KEY_UP 0
#define PLANE_KEY_DOWN 1
#define PLANE_KEY_LEFT 2
#define PLANE_KEY_RIGHT 3
#define PLANE_KEY_SHOOT 4
#define PLANE_KEY_START 5
#define PLANE_KEY_MENU 6

/* 玩家飞机结构 */
typedef struct {
    u8 x;        /* x坐标 */
    u8 y;        /* y坐标 */
    u8 width;    /* 宽度 */
    u8 height;   /* 高度 */
} PlayerPlane;

/* 敌机结构 */
typedef struct {
    u8 x;        /* x坐标 */
    u8 y;        /* y坐标 */
    u8 type;     /* 类型 0:小敌机 1:中敌机 2:大敌机 */
    u8 speed;    /* 移动速度 */
    u8 active;   /* 是否活跃 */
} EnemyPlane;

/* 子弹结构 */
typedef struct {
    u8 x;        /* x坐标 */
    u8 y;        /* y坐标 */
    u8 active;   /* 是否活跃 */
} Bullet;

/* 游戏全局变量 */
extern u8 planeGameState;
/* 【2026-09-22】分数从 u8 改成 u16 —— 一次齐射 10 枚、每枚最高 30 分，
 * 一轮就可能拿到 300 分，u8 的 255 上限会直接溢出（分数突然归零）。 */
extern u16 planeScore;
extern u16 planeHighScore;
extern u8 planeLives;
extern u8 planeLevel;
extern PlayerPlane player;
extern EnemyPlane enemies[10];  /* 最多10个敌机 */
extern Bullet bullets[PLANE_BULLET_MAX];   /* 一次齐射 10 枚 */

/* 函数声明 */
void PlaneGame_Init(void);
void PlaneGame_Update(void);
void PlaneGame_Draw(void);
void PlaneGame_HandleKey(u8 key);
void PlaneGame_SpawnEnemy(void);
void PlaneGame_Shoot(void);
u8 PlaneGame_CheckCollision(u8 x1, u8 y1, u8 w1, u8 h1, u8 x2, u8 y2, u8 w2, u8 h2);
void PlaneGame_ShowGameOver(void);

#endif /* __PLANE_GAME_H__ */