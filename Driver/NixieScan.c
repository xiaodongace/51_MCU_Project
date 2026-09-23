/*
 * NixieScan.c - 数码管 1ms 扫描（Timer2 中断驱动）
 *
 * Timer2 配置逐字照抄 v3.1 App/App_Digital.c 的 Timer_config()：
 *   16 位自动重装、1T 时钟源、初值 65536 - MAIN_Fosc/1000 = 1kHz、启用中断。
 *
 * 显示内容存在 s_buf[8] 里（每位一个 LED_TABLE 下标），中断只负责"轮流点亮"，
 * 这样中断代码极短，改内容也不影响扫描节奏。
 */
#include "NixieScan.h"
#include "GPIO.h"
#include "Timer.h"
#include "NVIC.h"
#include "App_Public.h"

/* 8 位要显示的内容（LED_TABLE 下标）。
 * 放 xdata：本项目用的是 Compact 内存模型，默认变量进 pdata（只有 256 字节），
 * 大一点的数组显式放 xdata 更稳妥。 */
static u8 xdata s_buf[NIXIE_DIGITS];
static u8 s_pos = 0;
static u8 s_running = 0;

/* Timer2 配置（照抄 v3.1 App_Digital.c） */
static void Timer2_Config(void)
{
    TIM_InitTypeDef TIM_InitStructure;

    /* 定时器2 做 16 位自动重装，中断频率 1000Hz */
    TIM_InitStructure.TIM_Mode      = TIM_16BitAutoReload;
    TIM_InitStructure.TIM_ClkSource = TIM_CLOCK_1T;
    TIM_InitStructure.TIM_ClkOut    = DISABLE;
    TIM_InitStructure.TIM_Value     = 65536UL - (MAIN_Fosc / 1000);
    TIM_InitStructure.TIM_PS        = 0;
    TIM_InitStructure.TIM_Run       = ENABLE;
    Timer_Inilize(Timer2, &TIM_InitStructure);

    NVIC_Timer2_Init(ENABLE, Priority_0);
}

void Nixie_ScanInit(void)
{
    u8 i;

    NIXIE_init();               /* 配 P4.2/P4.3/P4.4 为推挽 */

    for (i = 0; i < NIXIE_DIGITS; i++)
    {
        s_buf[i] = NIXIE_CH_BLANK;
    }

    s_pos = 0;

    /* 先全灭，避免上电残影 */
    NIXIE_show(0xFF, 0x00);

    Timer2_Config();
    s_running = 1;
}

/* pos 是"逻辑位号"，0 表示最左边。物理位号由 NIXIE_POS_REVERSED 决定 */
static u8 phys_pos(u8 pos)
{
#if NIXIE_POS_REVERSED
    return (u8)(NIXIE_DIGITS - 1 - pos);
#else
    return pos;
#endif
}

void Nixie_SetTime(u8 hour, u8 minute, u8 second)
{
    if (hour > 99)
    {
        hour = 99;
    }
    if (minute > 99)
    {
        minute = 99;
    }
    if (second > 99)
    {
        second = 99;
    }

    /* 布局：H H - M M - S S */
    s_buf[0] = (u8)(hour / 10);
    s_buf[1] = (u8)(hour % 10);
    s_buf[2] = NIXIE_CH_DASH;
    s_buf[3] = (u8)(minute / 10);
    s_buf[4] = (u8)(minute % 10);
    s_buf[5] = NIXIE_CH_DASH;
    s_buf[6] = (u8)(second / 10);
    s_buf[7] = (u8)(second % 10);
}

/*
 * 由 Timer2 中断每 1ms 调用一次。
 * 每次只点亮一位 —— 8 位轮一圈 8ms，125Hz，肉眼看不到闪烁。
 * 换位前先把上一位置灭（NIXIE_display 每次写的是"只有这一位"的位码，
 * 所以天然不会出现鬼影：位码只亮一位，其余位是 0）。
 */
void Nixie_Scan1ms(void)
{
    u8 id;

    if (!s_running)
    {
        return;
    }

    id = s_buf[s_pos];

    if (id == NIXIE_CH_BLANK || id > 35)
    {
        /* 这一位不显示：位码 0 表示 8 位全灭 */
        NIXIE_show(0xFF, 0x00);
    }
    else
    {
        NIXIE_display(id, phys_pos(s_pos));
    }

    s_pos++;
    if (s_pos >= NIXIE_DIGITS)
    {
        s_pos = 0;
    }
}
