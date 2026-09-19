/*
 * LED.c - 8 颗小灯 + 总开关
 *
 * 真值来源：v3.1 App/App_LED.c
 *   低电平点亮：LEDx = 0 -> 亮；LEDx = 1 -> 灭
 *   总开关低电平导通：LED_SW = 0 -> 打开；LED_SW = 1 -> 关闭
 *
 * 实现说明：LED1..LED8 是分布在 P1/P2 上的 sbit，C51 里 sbit 不能按下标访问，
 *          所以用 switch 逐个写。switch 展开成跳转表，比"位掩码 + 端口整体改写"更安全
 *          （后者容易误改同一端口上别的外设引脚）。
 */
#include "LED.h"
#include "GPIO.h"

static u8 s_ledCount = 0;   /* 当前点亮的颗数，0..8 */

/* 写第 idx 颗灯（idx = 0..7）。on: 1 = 亮（低电平），0 = 灭（高电平） */
static void led_write(u8 idx, u8 on)
{
    u8 lvl = on ? 0 : 1;

    switch (idx)
    {
    case 0: LED1 = lvl; break;
    case 1: LED2 = lvl; break;
    case 2: LED3 = lvl; break;
    case 3: LED4 = lvl; break;
    case 4: LED5 = lvl; break;
    case 5: LED6 = lvl; break;
    case 6: LED7 = lvl; break;
    case 7: LED8 = lvl; break;
    default: break;
    }
}

/* 上电安全电平：总开关关闭、8 灯全灭。
 * 只写 IO 口，不改端口模式，可以放在 EAXSFR() 之前调用。 */
void Led_SafeLevel(void)
{
    LED_SW = 1;                                     /* 1 = 关闭总开关 */
    LED1 = LED2 = LED3 = LED4 = 1;
    LED5 = LED6 = LED7 = LED8 = 1;
    s_ledCount = 0;
}

void Led_Power(u8 on)
{
    LED_SW = on ? 0 : 1;                            /* 0 = 导通 */
}

void Led_Init(void)
{
    /* 总开关 P4.5 推挽 */
    P4_MODE_OUT_PP(GPIO_Pin_5);
    /* P1.4 / P1.5 推挽 */
    P1_MODE_OUT_PP(GPIO_Pin_4 | GPIO_Pin_5);
    /* P2.7 P2.6 P2.3 P2.2 P2.1 P2.0 推挽（P2.4/P2.5 留给超声波与舵机，不能一起配） */
    P2_MODE_OUT_PP(GPIO_Pin_7 | GPIO_Pin_6 | GPIO_Pin_3 | GPIO_Pin_2 | GPIO_Pin_1 | GPIO_Pin_0);

    Led_SafeLevel();                                /* 先全灭，再决定要不要开总开关 */
}

void Led_AllOff(void)
{
    LED1 = LED2 = LED3 = LED4 = 1;
    LED5 = LED6 = LED7 = LED8 = 1;
    s_ledCount = 0;
}

void Led_AllOn(void)
{
    LED1 = LED2 = LED3 = LED4 = 0;
    LED5 = LED6 = LED7 = LED8 = 0;
    s_ledCount = LED_COUNT;
}

void Led_SetSingle(u8 idx, u8 on)
{
    if (idx >= LED_COUNT)
    {
        return;
    }

    led_write(idx, on ? 1 : 0);

    /* 同步颗数计数，方便查询 */
    if (on)
    {
        if (s_ledCount < LED_COUNT)
        {
            s_ledCount++;
        }
    }
    else
    {
        if (s_ledCount > 0)
        {
            s_ledCount--;
        }
    }
}

void Led_SetMask(u8 mask)
{
    u8 i;
    u8 cnt = 0;

    for (i = 0; i < LED_COUNT; i++)
    {
        u8 on = (mask >> i) & 1;
        led_write(i, on);
        if (on)
        {
            cnt++;
        }
    }

    s_ledCount = cnt;
}

/*
 * level 0..255 -> 点亮颗数 0..8
 * 用移位代替除法：level * 8 / 256 == level >> 5
 */
void Led_Breath(u8 level)
{
    u8 count = (u8)(level >> 5);        /* 0..7 */
    u8 i;

    /* level 最大 255 -> 255>>5 = 7，补上边界，让"全亮"能取到 8 */
    if (level >= 224)
    {
        count = LED_COUNT;
    }

    for (i = 0; i < LED_COUNT; i++)
    {
        led_write(i, (i < count) ? 1 : 0);
    }

    s_ledCount = count;
}

u8 Led_GetCount(void)
{
    return s_ledCount;
}
