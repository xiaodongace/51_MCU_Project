/*
 * Keys.c - 4 个独立按键，带 20ms 非阻塞消抖 + 2 秒长按
 *
 * 状态机（每个键独立）：
 *
 *   稳定抬起 --(原始电平变低)--> 记录变化时刻
 *   原始电平持续 20ms 都是低 --> 判定"稳定按下"，记录按下时刻，回调 on_keydown
 *   按住累计 >= 2000ms 且还没报过长按 --> 回调 on_longpress，置"已报长按"标记
 *   原始电平持续 20ms 都是高 --> 判定"稳定抬起"，若没报过长按则回调 on_keyup
 *
 * 说明：短按在"抬起"时上报，长按在"按满 2 秒"时上报。
 *       这样两种事件不会互相污染，长按也不会连发。
 */
#include "Keys.h"
#include "App_Public.h"

static u8  s_raw[KEY_COUNT];        /* 最近一次采样的原始电平（0=按下） */
static u8  s_stable[KEY_COUNT];     /* 已确认的稳定电平（0=按下） */
static u32 s_changeMs[KEY_COUNT];   /* 原始电平最近一次变化的时刻 */
static u32 s_pressMs[KEY_COUNT];    /* 稳定按下的时刻，用于算长按 */
static u8  s_longFired[KEY_COUNT];  /* 本次按住是否已经报过长按 */

/* 读取第 k 个按键的原始电平 */
static u8 key_read(u8 k)
{
    switch (k)
    {
    case 0: return KEY1;
    case 1: return KEY2;
    case 2: return KEY3;
    case 3: return KEY4;
    default: return KEY_LEVEL_UP;
    }
}

void Keys_Init(void)
{
    u8 i;
    u32 now;

    KEYS_GPIO_INIT();

    now = SysTick_Get();

    /* 用上电时的真实电平作为初始状态，避免把上电电平误判成一次按下 */
    for (i = 0; i < KEY_COUNT; i++)
    {
        s_raw[i]       = key_read(i);
        s_stable[i]    = s_raw[i];
        s_changeMs[i]  = now;
        s_pressMs[i]   = now;
        s_longFired[i] = 0;
    }
}

void Keys_Scan(void)
{
    u8 i;
    u8 raw;
    u32 now;

    now = SysTick_Get();

    for (i = 0; i < KEY_COUNT; i++)
    {
        raw = key_read(i);

        if (raw != s_raw[i])
        {
            /* 原始电平刚变化，可能还在抖动：只记录状态和时刻，不下结论 */
            s_raw[i]      = raw;
            s_changeMs[i] = now;
        }
        else if (raw != s_stable[i])
        {
            /* 原始电平持续不变且与稳定状态不同，检查是否已稳定满消抖时间 */
            if (SysTick_Elapsed(s_changeMs[i]) >= KEY_DEBOUNCE_MS)
            {
                s_stable[i] = raw;

                if (s_stable[i] == KEY_LEVEL_DOWN)
                {
                    /* 稳定按下 */
                    s_pressMs[i]   = now;
                    s_longFired[i] = 0;
                    Keys_on_keydown(i);
                }
                else
                {
                    /* 稳定抬起：没报过长按才算一次短按 */
                    if (!s_longFired[i])
                    {
                        Keys_on_keyup(i);
                    }
                    s_longFired[i] = 0;
                }
            }
        }

        /* 按住不动时，检查是否该报长按 */
        if (s_stable[i] == KEY_LEVEL_DOWN && !s_longFired[i])
        {
            if (SysTick_Elapsed(s_pressMs[i]) >= KEY_LONGPRESS_MS)
            {
                s_longFired[i] = 1;
                Keys_on_longpress(i);
            }
        }
    }
}
