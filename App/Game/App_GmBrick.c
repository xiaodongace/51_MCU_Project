/*------------------------------------------------------------------------
 *  【移植说明 · 2026-09-22】
 *  本文件移植自参考工程《23_基于stc8的多功能时钟》的 User/App_GmBrick.c。
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

#include "App_GmBrick.h"
#include "RTX51TNY.H"

/* 游戏全局变量定义 */
u8 brickState = GAME_RUNNING;
Paddle brickPaddle;
Ball brickBall;
Brick brickList[20];
u8 brickScore = 0;
u8 brickLives = 3;

/* 绝对值函数宏 */
#define ABS(x) ((x) < 0 ? -(x) : (x))

/**
 * @brief 初始化游戏
 */
void Brick_Init(void)
{
    u8 i, j, idx;

    /* 初始化挡板 */
    brickPaddle.x     = 64;
    brickPaddle.width = 20;

    /* 初始化球 */
    brickBall.x  = 64;
    brickBall.y  = 60;
    brickBall.dx = 1;
    brickBall.dy = -1;

    /* 初始化砖块 */
    idx = 0;
    for (j = 0; j < 4; j++) {
        for (i = 0; i < 5; i++) {
            brickList[idx].x       = i * 25 + 5;
            brickList[idx].y       = j * 8 + 5;
            brickList[idx].width   = 20;
            brickList[idx].height  = 6;
            brickList[idx].visible = 1;
            idx++;
        }
    }

    /* 清空显存 */
    SPI_OLED_GClear();

    /* 初始化分数和生命 */
    brickScore = 0;
    brickLives = 3;

    brickState = GAME_RUNNING;
}

/**
 * @brief 绘制游戏界面
 */
void Brick_Draw(void)
{
    u8 i, x, y;
    u8 brickPaddleLeft, brickPaddleRight;

    /* 清空显存 */
    SPI_OLED_GClear();

    /* 绘制挡板 */
    brickPaddleLeft  = brickPaddle.x - brickPaddle.width / 2;
    brickPaddleRight = brickPaddle.x + brickPaddle.width / 2;
    for (i = brickPaddleLeft; i <= brickPaddleRight; i++) {
        SPI_OLED_DrawPoint(i, 63);
    }

    /* 绘制球 */
    SPI_OLED_DrawPoint(brickBall.x, brickBall.y);

    /* 绘制砖块 */
    for (i = 0; i < 20; i++) {
        if (brickList[i].visible == 1) {
            /* 绘制砖块的四个边 */
            for (x = brickList[i].x; x < brickList[i].x + brickList[i].width; x++) {
                SPI_OLED_DrawPoint(x, brickList[i].y);
                SPI_OLED_DrawPoint(x, brickList[i].y + brickList[i].height);
            }
            for (y = brickList[i].y; y < brickList[i].y + brickList[i].height; y++) {
                SPI_OLED_DrawPoint(brickList[i].x, y);
                SPI_OLED_DrawPoint(brickList[i].x + brickList[i].width, y);
            }
        }
    }

    /* 刷新屏幕 */
    SPI_OLED_Refresh();
}

/**
 * @brief 更新游戏逻辑
 */
void Brick_Update(void)
{
    u8 i;
    u8 allBricksGone;

    if (brickState != GAME_RUNNING) {
        return;
    }

    /* 移动球 */
    brickBall.x += brickBall.dx;
    brickBall.y += brickBall.dy;

    /* 边界检测 */
    if (brickBall.x <= 0 || brickBall.x >= 127) {
        brickBall.dx = -brickBall.dx;
    }
    if (brickBall.y <= 0) {
        brickBall.dy = -brickBall.dy;
    }

    /* 球落到底部 */
    if (brickBall.y >= 63) {
        brickLives--;
        if (brickLives == 0) {
            brickState = GAME_OVER;
        } else {
            /* 重置球的位置 */
            brickBall.x  = 64;
            brickBall.y  = 60;
            brickBall.dx = (brickBall.dx > 0) ? 1 : -1;
            brickBall.dy = -1;
        }
    }

    /* 检测球与挡板碰撞 */
    if (brickBall.y >= 62 && brickBall.y <= 63) {
        if (brickBall.x >= (brickPaddle.x - brickPaddle.width / 2) &&
            brickBall.x <= (brickPaddle.x + brickPaddle.width / 2)) {
            brickBall.dy = -brickBall.dy;
            /* 根据击中挡板的位置调整反弹角度。
             * 【2026-09-22 修正】原来直接 (x - paddle)/4：击中挡板正中时
             * 算出来是 0 —— 球变成纯上下运动，卡在一条竖线上来回弹，
             * 左右再也不推进，砖块永远打不完。这里保证"至少偏向一侧 1 像素"。 */
            {
                s8 d = (s8)(((s16)brickBall.x - (s16)brickPaddle.x) / 4);
                if (d == 0)
                {
                    d = (s8)((brickBall.x >= brickPaddle.x) ? 1 : -1);
                }
                brickBall.dx = d;
            }
        }
    }

    /* 检测球与砖块碰撞 */
    for (i = 0; i < 20; i++) {
        if (brickList[i].visible == 1) {
            if (brickBall.x >= brickList[i].x &&
                brickBall.x <= brickList[i].x + brickList[i].width &&
                brickBall.y >= brickList[i].y &&
                brickBall.y <= brickList[i].y + brickList[i].height) {
                brickList[i].visible = 0;
                brickScore += 10;

                /* 确定从哪个方向碰撞 */
                if (brickBall.x <= brickList[i].x + 2 ||
                    brickBall.x >= brickList[i].x + brickList[i].width - 2) {
                    brickBall.dx = -brickBall.dx;
                } else {
                    brickBall.dy = -brickBall.dy;
                }

                break;
            }
        }
    }

    /* 检查是否所有砖块都被击破 */
    allBricksGone = 1;
    for (i = 0; i < 20; i++) {
        if (brickList[i].visible == 1) {
            allBricksGone = 0;
            break;
        }
    }

    if (allBricksGone) {
        brickState = GAME_OVER;
    }
}

/**
 * @brief 移动挡板
 * @param direction 移动方向: -1左移, 1右移
 */
void Brick_MovePaddle(char direction)
{
    if (brickState != GAME_RUNNING) {
        return;
    }

    brickPaddle.x += direction * 3;

    /* 限制挡板不超出屏幕 */
    if (brickPaddle.x < brickPaddle.width / 2) {
        brickPaddle.x = brickPaddle.width / 2;
    }
    if (brickPaddle.x > 127 - brickPaddle.width / 2) {
        brickPaddle.x = 127 - brickPaddle.width / 2;
    }
}

/**
 * @brief 显示游戏结束画面
 */
void Brick_ShowGameOver(void)
{
    u8 buf[20];

    /* 【2026-09-23 修"结算画面全黑、看不到分数"】
     *
     * 主屏有两条**互不相容**的通路（完整说明见 Driver/SPI_OLED/spi_oled.h）：
     *     A 直写屏：SPI_OLED_Clear() + SPI_OLED_Display_GB2312_string()
     *               —— 菜单/页面级界面走这条
     *     B 显存：  SPI_OLED_GClear() + SPI_OLED_DrawPoint() + SPI_OLED_Refresh()
     *               —— 本游戏走这条（见 Brick_Draw）
     *
     * 上一版这里把两条**混用**了：
     *     GClear()                          // 清的是**显存**
     *     Display_GB2312_string(...)        // 直写屏，压根没往显存里写
     *     Refresh()                         // 把（空的）显存整屏覆盖上去
     * -> 刚写好的字被自己擦掉 -> 结算画面全黑 3 秒。
     *
     * 现在全部走显存通路：先 GClear 清显存，再用 GBuf_string 往显存里写字，
     * 最后一次 Refresh 统一上屏 —— 与 Brick_Draw 内部完全一致。 */
    SPI_OLED_GClear();

    /* 坐标：128x64 屏，按"页"定位（1 页 = 8 像素高）
     *   "GAME OVER" 9 个半角字符 x 8 = 72 宽 -> 居中 x = (128-72)/2 = 28
     *   "SCORE:n"  最长 9 字符 = 72 宽 -> 也用 28
     * 取页 2 与页 4（屏幕中部偏上），醒目、不贴边。 */
    SPI_OLED_GBuf_string(28, 2, (u8 *)"GAME OVER");

    sprintf((char *)buf, "SCORE:%d", (int)brickScore);
    SPI_OLED_GBuf_string(28, 4, buf);

    SPI_OLED_Refresh();
}

