/*------------------------------------------------------------------------
 *  【移植说明 · 2026-09-22】
 *  本文件移植自参考工程《23_基于stc8的多功能时钟》的 User/App_GmSnake.c。
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

#include "App_GmSnake.h"
#include "RTX51TNY.H"

/* 游戏全局变量 */
u8 snakeGameState    = SNAKE_STATE_PLAYING;
u8 snakeDirection    = SNAKE_DIR_RIGHT;
u8 foodX             = 0;
u8 foodY             = 0;
u8 snakeScore        = 0;
u8 snakeHighScore    = 0;
SnakeNode *snakeHead = NULL;

/* 绝对值宏 */
#define ABS(x) ((x) < 0 ? -(x) : (x))

/*------------------------------------------------------------------------
 *  【移植改动】把链表节点的 malloc/free 换成**静态节点池**
 *
 *  为什么必须换：C51 上不用堆 ——
 *    · 堆会链入一大坨库代码，占用本来就紧的 Flash；
 *    · 8051 只有 8KB xdata，碎片化之后不可控；
 *    · 本工程硬约束：不用动态分配。
 *  做法很朴素：开一块固定数组当池子，配一个**空闲链表**，
 *  snake_alloc()/snake_free() 就是"从空闲链表摘一个 / 挂回去"。
 *  游戏逻辑（Snake_Init/Snake_Update/Snake_Draw）一行没动。
 *------------------------------------------------------------------------*/
#define SNAKE_MAX_LEN   40          /* 蛇最长 40 节，够玩 */

static SnakeNode xdata s_snakePool[SNAKE_MAX_LEN];
/* 前置声明：池子的两个操作在使用它们之前先声明一次，
 * 免得受"函数定义顺序"影响（这次移植就踩到了 C267）。 */
static SnakeNode *snake_alloc(void);
static void       snake_free(SnakeNode *n);

static SnakeNode *s_snakeFree = NULL;

static void snake_pool_reset(void)
{
    u8 i;

    for (i = 0; i < SNAKE_MAX_LEN; i++)
    {
        s_snakePool[i].next = (i + 1 < SNAKE_MAX_LEN) ? &s_snakePool[i + 1] : NULL;
    }
    s_snakeFree = &s_snakePool[0];
}

static SnakeNode *snake_alloc(void)
{
    SnakeNode *n;

    if (s_snakeFree == NULL)
    {
        return NULL;                /* 池子空了（蛇太长）—— 返回 NULL，调用方自己处理 */
    }

    n = s_snakeFree;
    s_snakeFree = s_snakeFree->next;
    n->next = NULL;
    return n;
}

static void snake_free(SnakeNode *n)
{
    if (n == NULL)
    {
        return;
    }

    n->next = s_snakeFree;
    s_snakeFree = n;
}

/**
 * @brief 初始化贪吃蛇游戏
 */
void Snake_Init(void)
{
    SnakeNode *newHead;

    /* ============================================================
     * 【2026-09-22 修"贪吃蛇一进去就不动"的根因】
     *
     * 静态节点池必须先 reset 一次！原来移植时漏了这一步：
     *   s_snakeFree 的初值是 NULL，而唯一给它赋值的 snake_pool_reset()
     *   全工程从来没被调用过 —— 于是 snake_alloc() 永远返回 NULL：
     *       Snake_Init   里 newHead->x = 16;   写的是空指针
     *       snakeHead 也是 NULL
     *       Snake_Update 里 newX = snakeHead->x;   读地址 0 的垃圾
     *       Snake_CheckCollision 里遍历 snakeHead->next   走垃圾指针
     *   现象就是"进游戏后蛇完全不动"，而且按键也没反应。
     *
     * 原版（参考工程）用 malloc，分配时天然就有节点，不存在这一步；
     * 换成静态池之后，"池子归位"这件事必须自己做。
     *
     * reset 之后池子里所有节点都是空闲的，所以旧蛇不用再逐个 free ——
     * 直接把头指针丢掉即可（节点已随池子一起回收）。
     * ============================================================ */
    snake_pool_reset();
    snakeHead = NULL;

    /* 创建蛇头。
     * 【出生点】放在**左侧 x=16**（原来在屏幕正中 x=64）。
     *   到右墙有 13 步；配合当前节拍 GAME_STEP_MS_SNAKE = 250ms
     *   （定义在 App_Menu.c）就是约 3.3 秒的操作窗口，正常能玩。
     *   ※ 更早那版节拍是 100ms，13 步只有 1.3 秒，真机反馈"速度极快"，已放慢。
     *     节拍值现在按**毫秒**写在 App_Menu.c，不再写"多少拍"。 */
    newHead = (SnakeNode *)snake_alloc();
    if (newHead == NULL)
    {
        /* 池子拿不到节点：reset 之后不该发生。真发生就判结束，
         * 绝不去写空指针 —— 那是"静默跑飞"。 */
        snakeGameState = SNAKE_STATE_GAME_OVER;
        return;
    }
    newHead->x    = 16;
    newHead->y    = 32;
    newHead->next = NULL;
    snakeHead     = newHead;

    /* 初始化方向 */
    snakeDirection = SNAKE_DIR_RIGHT;

    /* 生成食物 */
    Snake_GenerateFood();

    /* 重置分数 */
    snakeScore = 0;

    /* 清屏 */
    SPI_OLED_GClear();

    snakeGameState = SNAKE_STATE_PLAYING;
}

/**
 * @brief 生成食物
 */
void Snake_GenerateFood(void)
{
    u8 valid = 0;
    SnakeNode *current;

    while (!valid) {
        /* 随机生成食物位置 */
        foodX = (rand() % 16) * 8; /* 0-127, 8像素对齐 */
        foodY = (rand() % 8) * 8;  /* 0-63, 8像素对齐 */

        /* 检查是否与蛇身重叠 */
        valid   = 1;
        current = snakeHead;
        while (current != NULL) {
            if (current->x == foodX && current->y == foodY) {
                valid = 0;
                break;
            }
            current = current->next;
        }
    }
}

/**
 * @brief 检查碰撞
 * @param x 要检查的x坐标
 * @param y 要检查的y坐标
 * @return 1表示碰撞，0表示安全
 */
u8 Snake_CheckCollision(u8 x, u8 y)
{
    SnakeNode *current;

    /* 边界检查（屏幕 128x64）。
     * ※ x/y 是 u8：向外撞墙时 newX = x-8 会**回绕**成 248 这种大数，
     *   所以只需要判"超过上界"就够了。
     *   （原来的 x < 0 / y < 0 对无符号数恒为假，是两句死判断，已去掉。）*/
    if (x >= 128 || y >= 64) {
        return 1;
    }

    /* 蛇身碰撞检查（跳过蛇头） */
    if (snakeHead == NULL) {
        return 0;               /* 没有蛇头：不算碰撞（正常走不到）*/
    }

    current = snakeHead->next;
    while (current != NULL) {
        if (current->x == x && current->y == y) {
            return 1;
        }
        current = current->next;
    }

    return 0;
}

/**
 * @brief 更新游戏逻辑
 */
void Snake_Update(void)
{
    u8 newX, newY;
    SnakeNode *newHead;
    SnakeNode *current;

    if (snakeGameState != SNAKE_STATE_PLAYING) {
        return;
    }

    if (snakeHead == NULL)
    {
        snakeGameState = SNAKE_STATE_GAME_OVER;
        return;
    }

    /* 计算新的蛇头位置 */
    newX = snakeHead->x;
    newY = snakeHead->y;

    switch (snakeDirection) {
        case SNAKE_DIR_UP:
            newY -= 8;
            break;
        case SNAKE_DIR_RIGHT:
            newX += 8;
            break;
        case SNAKE_DIR_DOWN:
            newY += 8;
            break;
        case SNAKE_DIR_LEFT:
            newX -= 8;
            break;
    }

    /* 检查碰撞 */
    if (Snake_CheckCollision(newX, newY)) {
        snakeGameState = SNAKE_STATE_GAME_OVER;
        if (snakeScore > snakeHighScore) {
            snakeHighScore = snakeScore;
        }
        return;
    }

    /* 创建新的蛇头 */
    newHead = (SnakeNode *)snake_alloc();
    if (newHead == NULL)
    {
        /* 池子空了 = 蛇到了长度上限（SNAKE_MAX_LEN），判本局结束 */
        snakeGameState = SNAKE_STATE_GAME_OVER;
        if (snakeScore > snakeHighScore) {
            snakeHighScore = snakeScore;
        }
        return;
    }
    newHead->x    = newX;
    newHead->y    = newY;
    newHead->next = snakeHead;
    snakeHead     = newHead;

    /* 检查是否吃到食物 */
    if (newX == foodX && newY == foodY) {
        snakeScore++;
        Snake_GenerateFood();
    } else {
        /* 没吃到食物，移除蛇尾 */
        current = snakeHead;
        while (current->next->next != NULL) {
            current = current->next;
        }
        snake_free(current->next);
        current->next = NULL;
    }
}

/**
 * @brief 绘制游戏界面
 */
void Snake_Draw(void)
{
    SnakeNode *current;
    u8 i;

    /* 清空显存 */
    SPI_OLED_GClear();

    /* 绘制蛇身 */
    current = snakeHead;
    while (current != NULL) {
        /* 绘制蛇身方块（2x2像素点） */
        for (i = 0; i < 2; i++) {
            SPI_OLED_DrawPoint(current->x + i, current->y);
            SPI_OLED_DrawPoint(current->x + i, current->y + 1);
        }
        current = current->next;
    }

    /* 绘制食物 */
    for (i = 0; i < 4; i++) {
        SPI_OLED_DrawPoint(foodX + i, foodY);
        SPI_OLED_DrawPoint(foodX + i, foodY + 1);
    }

    /* 绘制边界 */
    for (i = 0; i < 128; i++) {
        SPI_OLED_DrawPoint(i, 0);
        SPI_OLED_DrawPoint(i, 63);
    }
    for (i = 0; i < 64; i++) {
        SPI_OLED_DrawPoint(0, i);
        SPI_OLED_DrawPoint(127, i);
    }

    /* 刷新屏幕 */
    SPI_OLED_Refresh();
}

/**
 * @brief 处理输入
 * @param direction 新的方向
 */
void Snake_HandleInput(u8 direction)
{
    /* 防止180度转向 */
    if ((snakeDirection == SNAKE_DIR_UP && direction == SNAKE_DIR_DOWN) ||
        (snakeDirection == SNAKE_DIR_DOWN && direction == SNAKE_DIR_UP) ||
        (snakeDirection == SNAKE_DIR_LEFT && direction == SNAKE_DIR_RIGHT) ||
        (snakeDirection == SNAKE_DIR_RIGHT && direction == SNAKE_DIR_LEFT)) {
        return;
    }

    snakeDirection = direction;
}


/**
 * @brief 显示游戏结束画面
 */
void Snake_ShowGameOver(void)
{
    u8 buf[20];

    /* 【2026-09-23 修"结算画面全黑、看不到分数"】
     *
     * 主屏有两条**互不相容**的通路（完整说明见 Driver/SPI_OLED/spi_oled.h）：
     *     A 直写屏：SPI_OLED_Clear() + SPI_OLED_Display_GB2312_string()
     *               —— 菜单/页面级界面走这条
     *     B 显存：  SPI_OLED_GClear() + SPI_OLED_DrawPoint() + SPI_OLED_Refresh()
     *               —— 本游戏走这条（见 Snake_Draw）
     *
     * 上一版这里把两条**混用**了：
     *     GClear()                          // 清的是**显存**
     *     Display_GB2312_string(...)        // 直写屏，压根没往显存里写
     *     Refresh()                         // 把（空的）显存整屏覆盖上去
     * -> 刚写好的字被自己擦掉 -> 结算画面全黑 3 秒。
     *
     * 现在全部走显存通路：先 GClear 清显存，再用 GBuf_string 往显存里写字，
     * 最后一次 Refresh 统一上屏 —— 与 Snake_Draw 内部完全一致。 */
    SPI_OLED_GClear();

    /* 坐标：128x64 屏，按"页"定位（1 页 = 8 像素高）
     *   "GAME OVER" 9 个半角字符 x 8 = 72 宽 -> 居中 x = (128-72)/2 = 28
     *   "SCORE:n"  最长 9 字符 = 72 宽 -> 也用 28
     * 取页 2 与页 4（屏幕中部偏上），醒目、不贴边。 */
    SPI_OLED_GBuf_string(28, 2, (u8 *)"GAME OVER");

    sprintf((char *)buf, "SCORE:%d", (int)snakeScore);
    SPI_OLED_GBuf_string(28, 4, buf);

    SPI_OLED_Refresh();
}

