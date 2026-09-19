/*
 * App_Game.c - 掌机模式占位实现（M1）
 *
 * 现在能做什么：长按 KEY3/KEY4 会进入"掌机模式"，主屏显示占位页，
 *              副屏标题显示 GAME HALL，按 KEY2 返回主界面。
 * 现在不能做什么：没有任何游戏逻辑 —— 那是 M2 的事。
 *
 * 这样做的价值：把"M1 必须为 M2 留位"这件事从文档里变成可运行的代码路径，
 *              避免 M2 开工时发现页面编号、存储分区、定时器都要改。
 */
#include "App_Game.h"

u8 Game_IsGamePage(UIPageId_t page)
{
    switch (page)
    {
    case PAGE_GAME_HALL:
    case PAGE_GAME_SNAKE:
    case PAGE_GAME_BRICK:
    case PAGE_GAME_PLANE:
    case PAGE_GAME_DAILY:
    case PAGE_GAME_OVER:
        return 1;
    default:
        return 0;
    }
}

void Game_Init(void)
{
    /* M1 没有需要初始化的东西。
     * M2 在这里准备游戏状态（贪吃蛇身体、砖块位置等，约 0.6KB，见《02》5.2）。 */
}

u8 Game_Enter(void)
{
    printf("[GAME] game hall reserved for M2, page framework ready\r\n");

    /* M2 改成"检查内存配额是否满足，满足才返回 1" */
    return 1;
}

void Game_OnEvent(const Event_t *evt)
{
    /* M1：掌机页面除返回键外不响应任何按键。
     * 返回键由 App_Menu.c 的 default 分支统一处理（调用 Menu_Back）。 */
    if (evt == NULL)
    {
        return;
    }

    printf("[GAME] event type=%d ignored (M1 placeholder)\r\n", (int)evt->type);
}

void Game_Tick1s(void)
{
    /* M1 空实现。M2 在这里推游戏主循环的秒级逻辑。 */
}
