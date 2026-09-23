/*------------------------------------------------------------------------
 *  【移植说明 · 2026-09-22】
 *  本文件移植自参考工程《23_基于stc8的多功能时钟》的 User/App_GmPlane.c。
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

#include "App_GmPlane.h"
/* 游戏全局变量 */
u8 planeGameState = PLANE_STATE_MENU;
u16 planeScore     = 0;
u16 planeHighScore = 0;
u8 planeLives     = 3;
u8 planeLevel     = 1;
PlayerPlane player;
EnemyPlane enemies[10];
Bullet bullets[PLANE_BULLET_MAX];

/* 游戏计时器 */
u32 planeLastUpdateTime = 0;
u32 planeEnemySpawnTime = 0;

/**
 * @brief 初始化飞机游戏
 */
void PlaneGame_Init(void)
{
    u8 i;

    /* 初始化玩家飞机 */
    player.x      = 64;
    player.y      = 50;
    player.width  = 12;
    player.height = 8;

    /* 初始化敌机 */
    for (i = 0; i < 10; i++) {
        enemies[i].active = 0;
    }

    /* 初始化子弹 */
    for (i = 0; i < PLANE_BULLET_MAX; i++) {
        bullets[i].active = 0;
    }

    /* 重置分数和生命 */
    planeScore = 0;
    planeLives = 3;
    planeLevel = 1;

    planeGameState = PLANE_STATE_PLAYING;

    /* 清屏 */
    SPI_OLED_GClear();
}

/**
 * @brief 更新游戏逻辑
 */
void PlaneGame_Update(void)
{
    u8 i, j;

    if (planeGameState != PLANE_STATE_PLAYING) {
        return;
    }

    /* 移动子弹 */
    for (i = 0; i < PLANE_BULLET_MAX; i++) {
        if (bullets[i].active) {
            /* 【2026-09-22 修正】原来先做 y -= 4、再判 y < 0。
             * 但 y 是 u8：y = 0..3 时减 4 会**回绕**成 252..255，
             * "y < 0" 永远不成立 => 子弹永不失效，5 个弹位很快被占满，
             * 之后再也发不出子弹（玩家表现为"打着打着不会开火了"）。
             * 正确做法：先判断"再走一步会不会越界"，越界就直接释放弹位。 */
            if (bullets[i].y < 4U)
            {
                bullets[i].active = 0;
            }
            else
            {
                bullets[i].y -= 4; /* 子弹向上移动 */
            }
        }
    }

    /* 移动敌机 */
    for (i = 0; i < 10; i++) {
        if (enemies[i].active) {
            enemies[i].y += enemies[i].speed; /* 敌机向下移动 */

            /* 敌机超出屏幕 */
            if (enemies[i].y > 63) {
                enemies[i].active = 0;
            }
        }
    }

    /* 检测子弹与敌机碰撞 */
    for (i = 0; i < PLANE_BULLET_MAX; i++) {
        /* 【2026-09-22 修正】原来外层只有这一句 if，命中后虽然把
         * bullets[i].active 清了，**但没有 break**，内层循环会继续跑完，
         * 于是一发子弹能连续打穿同一帧里的多个敌机（一发多杀、白送分）。
         * 现在命中就跳出内层循环，做到"一发子弹只消耗在一架敌机上"。 */
        if (bullets[i].active) {
            for (j = 0; j < 10; j++) {
                if (enemies[j].active) {
                    u8 enemyWidth  = 8;
                    u8 enemyHeight = 8;

                    if (enemies[j].type == 1) {
                        enemyWidth  = 12;
                        enemyHeight = 10;
                    } else if (enemies[j].type == 2) {
                        enemyWidth  = 16;
                        enemyHeight = 12;
                    }

                    if (PlaneGame_CheckCollision(
                            bullets[i].x, bullets[i].y, 2, 4,
                            enemies[j].x, enemies[j].y, enemyWidth, enemyHeight)) {
                        /* 击中敌机 */
                        bullets[i].active = 0;
                        enemies[j].active = 0;

                        /* 根据敌机类型加分 */
                        planeScore += (enemies[j].type + 1) * 10;

                        /* 检查是否升级 */
                        if (planeScore >= planeLevel * 100) {
                            planeLevel++;
                        }

                        break;      /* 这发子弹已用完，不再检查其它敌机 */
                    }
                }
            }
        }
    }

    /* 检测玩家与敌机碰撞 */
    for (i = 0; i < 10; i++) {
        if (enemies[i].active) {
            u8 enemyWidth  = 8;
            u8 enemyHeight = 8;

            if (enemies[i].type == 1) {
                enemyWidth  = 12;
                enemyHeight = 10;
            } else if (enemies[i].type == 2) {
                enemyWidth  = 16;
                enemyHeight = 12;
            }

            if (PlaneGame_CheckCollision(
                    player.x, player.y, player.width, player.height,
                    enemies[i].x, enemies[i].y, enemyWidth, enemyHeight)) {
                /* 玩家被击中 */
                enemies[i].active = 0;
                planeLives--;

                if (planeLives == 0) {
                    planeGameState = PLANE_STATE_GAME_OVER;
                    if (planeScore > planeHighScore) {
                        planeHighScore = planeScore;
                    }
                }
            }
        }
    }

    /* 生成新敌机 */
    if (rand() % 100 < 10 + planeLevel) /* 随等级增加生成概率 */
    {
        PlaneGame_SpawnEnemy();
    }
}

/**
 * @brief 绘制游戏界面
 */
void PlaneGame_Draw(void)
{
    u8 i;

    /* 清空显存 */
    SPI_OLED_GClear();

    /* 绘制玩家飞机 */
    for (i = 0; i < player.width; i++) {
        SPI_OLED_DrawPoint(player.x + i, player.y);
        SPI_OLED_DrawPoint(player.x + i, player.y + player.height - 1);
    }
    for (i = 0; i < player.height; i++) {
        SPI_OLED_DrawPoint(player.x, player.y + i);
        SPI_OLED_DrawPoint(player.x + player.width - 1, player.y + i);
    }

    /* 绘制敌机 */
    for (i = 0; i < 10; i++) {
        if (enemies[i].active) {
            u8 j;
            u8 width  = 8;
            u8 height = 8;

            if (enemies[i].type == 1) {
                width  = 12;
                height = 10;
            } else if (enemies[i].type == 2) {
                width  = 16;
                height = 12;
            }

            /* 绘制敌机边框 */
            for (j = 0; j < width; j++) {
                SPI_OLED_DrawPoint(enemies[i].x + j, enemies[i].y);
                SPI_OLED_DrawPoint(enemies[i].x + j, enemies[i].y + height - 1);
            }
            for (j = 0; j < height; j++) {
                SPI_OLED_DrawPoint(enemies[i].x, enemies[i].y + j);
                SPI_OLED_DrawPoint(enemies[i].x + width - 1, enemies[i].y + j);
            }
        }
    }

    /* 绘制子弹 */
    for (i = 0; i < PLANE_BULLET_MAX; i++) {
        if (bullets[i].active) {
            SPI_OLED_DrawPoint(bullets[i].x, bullets[i].y);
            SPI_OLED_DrawPoint(bullets[i].x, bullets[i].y + 1);
            SPI_OLED_DrawPoint(bullets[i].x, bullets[i].y + 2);
            SPI_OLED_DrawPoint(bullets[i].x, bullets[i].y + 3);
        }
    }

    /* 刷新屏幕 */
    SPI_OLED_Refresh();
}

/**
 * @brief 处理按键输入
 * @param key 按键代码
 */
void PlaneGame_HandleKey(u8 key)
{
    switch (key) {
        case PLANE_KEY_UP:
            if (planeGameState == PLANE_STATE_PLAYING && player.y > 2) {
                player.y -= 2;
            }
            break;

        case PLANE_KEY_DOWN:
            if (planeGameState == PLANE_STATE_PLAYING && player.y < 63 - player.height) {
                player.y += 2;
            }
            break;

        case PLANE_KEY_LEFT:
            if (planeGameState == PLANE_STATE_PLAYING && player.x > 2) {
                player.x -= 2;
            }
            break;

        case PLANE_KEY_RIGHT:
            if (planeGameState == PLANE_STATE_PLAYING && player.x < 127 - player.width) {
                player.x += 2;
            }
            break;

        case PLANE_KEY_SHOOT:
            if (planeGameState == PLANE_STATE_PLAYING) {
                PlaneGame_Shoot();
            }
            break;

        case PLANE_KEY_START:
            if (planeGameState == PLANE_STATE_MENU || planeGameState == PLANE_STATE_GAME_OVER) {
                PlaneGame_Init();
            }
            break;

        case PLANE_KEY_MENU:
            planeGameState = PLANE_STATE_MENU;
            break;
    }
}

/**
 * @brief 发射子弹 —— 【2026-09-22 用户要求】一次齐射 10 枚
 *
 *  改动前：找**第一个**空闲槽、只发 1 枚（原版就是这样，靠外壳每 90ms 调一次）。
 *  改动后：一次把所有空闲槽填满（最多 PLANE_BULLET_MAX = 10 枚），
 *          x 位置以飞机中心为准**横向铺开 18 像素**，形成一排弹幕。
 *
 *  为什么横向铺开而不是全堆在机头：10 枚叠在同一点，屏幕上只看得到 1 枚，
 *  射击的爽感和命中面积都体现不出来；铺开之后一排扫过去，手感才对。
 *
 *  ※ 冷却（3 秒）由外壳 App_Menu.c 的 game_fire() 管，这里不判冷却 ——
 *    游戏源码只管"怎么发射"，"什么时候能发射"是外壳的规则。
 */
void PlaneGame_Shoot(void)
{
    u8  i;
    u8  k = 0;                  /* 本次已经发出的枚数（0..9） */
    s16 bx;                     /* 中间用有符号算，避免 u8 下溢 */

    if (planeGameState != PLANE_STATE_PLAYING) {
        return;
    }

    for (i = 0; i < PLANE_BULLET_MAX && k < PLANE_BULLET_MAX; i++) {
        if (!bullets[i].active) {
            /* 以飞机中心 (player.x + width/2) 为基准，向左 9 像素、向右 9 像素铺开，
             * 每枚间隔 2 像素：偏移 = k*2 - 9 -> k=0..9 时为 -9,-7,...,+9 */
            bx = (s16)player.x + (s16)(player.width / 2) + (s16)(k * 2) - 9;

            /* 夹到屏幕内。x 是 u8，不加这个夹取的话，飞机贴左墙时
             * bx 会算成 -1，存进 u8 变成 255 —— 子弹从屏幕另一头冒出来。 */
            if (bx < 1)   { bx = 1;   }
            if (bx > 126) { bx = 126; }

            bullets[i].x      = (u8)bx;
            bullets[i].y      = player.y;
            bullets[i].active = 1;
            k++;
        }
    }
}

/**
 * @brief 生成敌机
 */
void PlaneGame_SpawnEnemy(void)
{
    u8 i;

    /* 寻找空闲的敌机槽 */
    for (i = 0; i < 10; i++) {
        if (!enemies[i].active) {
            enemies[i].x      = rand() % (128 - 16);
            enemies[i].y      = 0;
            enemies[i].type   = rand() % 3;                      /* 随机敌机类型 */
            /* 【2026-09-22】速度封顶 4。原来只有"随等级无限加速"，
             * 等级高了之后敌机每帧走 7~8 像素（30ms 一帧）= 250px/s，
             * 屏幕只有 64 行高，0.25 秒就穿过去 —— 根本躲不开。
             * 上加一个上限，让高等级仍然可玩。 */
            enemies[i].speed  = 1 + rand() % 2 + planeLevel / 2; /* 随等级增加速度 */
            if (enemies[i].speed > 4U) {
                enemies[i].speed = 4U;
            }
            enemies[i].active = 1;
            break;
        }
    }
}

/**
 * @brief 检查碰撞
 * @return 1表示碰撞，0表示无碰撞
 */
u8 PlaneGame_CheckCollision(u8 x1, u8 y1, u8 w1, u8 h1, u8 x2, u8 y2, u8 w2, u8 h2)
{
    return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}


/**
 * @brief 显示游戏结束画面
 */
void PlaneGame_ShowGameOver(void)
{
    u8 buf[20];

    /* 【2026-09-23 修"结算画面全黑、看不到分数"】
     *
     * 主屏有两条**互不相容**的通路（完整说明见 Driver/SPI_OLED/spi_oled.h）：
     *     A 直写屏：SPI_OLED_Clear() + SPI_OLED_Display_GB2312_string()
     *               —— 菜单/页面级界面走这条
     *     B 显存：  SPI_OLED_GClear() + SPI_OLED_DrawPoint() + SPI_OLED_Refresh()
     *               —— 本游戏走这条（见 PlaneGame_Draw）
     *
     * 上一版这里把两条**混用**了：
     *     GClear()                          // 清的是**显存**
     *     Display_GB2312_string(...)        // 直写屏，压根没往显存里写
     *     Refresh()                         // 把（空的）显存整屏覆盖上去
     * -> 刚写好的字被自己擦掉 -> 结算画面全黑 3 秒。
     *
     * 现在全部走显存通路：先 GClear 清显存，再用 GBuf_string 往显存里写字，
     * 最后一次 Refresh 统一上屏 —— 与 PlaneGame_Draw 内部完全一致。 */
    SPI_OLED_GClear();

    /* 坐标：128x64 屏，按"页"定位（1 页 = 8 像素高）
     *   "GAME OVER" 9 个半角字符 x 8 = 72 宽 -> 居中 x = (128-72)/2 = 28
     *   "SCORE:n"  最长 9 字符 = 72 宽 -> 也用 28
     * 取页 2 与页 4（屏幕中部偏上），醒目、不贴边。 */
    SPI_OLED_GBuf_string(28, 2, (u8 *)"GAME OVER");

    sprintf((char *)buf, "SCORE:%d", (int)planeScore);
    SPI_OLED_GBuf_string(28, 4, buf);

    SPI_OLED_Refresh();
}

