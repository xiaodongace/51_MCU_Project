/*
 * MatrixKey.c - 4x4 矩阵键盘扫描（带 20ms 非阻塞消抖）
 *
 * 扫描方法照抄 v3.1 Driver/MatrixKey.c：
 *   外循环把第 row 行拉低（其余行拉高），内循环读 4 个列电平；
 *   拉低行之后要 NOP2() 等信号稳定再读（v3.1 注释："等待 GPIO 引脚信号稳定"）。
 *
 * 本工程把 v3.1 的"逐位比较"改成"先拼 16 位再统一比较"，
 * 好处是消抖时刻可以按位记录，代码也短一半。
 */
#include "MatrixKey.h"
#include "App_Public.h"

/* 16 位的原始电平位掩码，1 = 抬起（高），0 = 按下（低） */
static u16 s_raw    = 0xFFFF;
static u16 s_stable = 0xFFFF;

/* 每个键的原始电平最近一次变化时刻（单位 ms，用 u16 存，差值比较天然抗回绕） */
static u16 s_changeMs[MK_KEY_COUNT];

/* 把第 row 行拉低，其余行拉高。row = 4 表示"所有行都拉高"（恢复原状） */
static void row_out(u8 row)
{
    ROW1 = (row == 0) ? 0 : 1;
    ROW2 = (row == 1) ? 0 : 1;
    ROW3 = (row == 2) ? 0 : 1;
    ROW4 = (row == 3) ? 0 : 1;
}

static u8 col_in(u8 col)
{
    switch (col)
    {
    case 0: return COL1;
    case 1: return COL2;
    case 2: return COL3;
    case 3: return COL4;
    default: return 1;
    }
}

void MK_Init(void)
{
    u8 i;
    u16 now;

    MK_GPIO_INIT();

    /* 恢复：所有行拉高，不选中任何一行 */
    row_out(MK_ROW_NUM);

    now = (u16)SysTick_Get();

    s_raw    = 0xFFFF;
    s_stable = 0xFFFF;

    for (i = 0; i < MK_KEY_COUNT; i++)
    {
        s_changeMs[i] = now;
    }
}

void MK_Scan(void)
{
    u8 r;
    u8 c;
    u8 pos;
    u16 rawNow = 0;
    u16 diff;
    u16 now;

    /* 1) 采样：逐行拉低，读 4 个列，拼成 16 位原始电平 */
    for (r = 0; r < MK_ROW_NUM; r++)
    {
        NOP2();                 /* 先给一点建立时间 */
        row_out(r);
        NOP2();                 /* 行电平变了，等信号稳定再读（照抄 v3.1） */

        for (c = 0; c < MK_COL_NUM; c++)
        {
            if (col_in(c))
            {
                pos = (u8)(r * MK_COL_NUM + c);
                rawNow |= (u16)(1UL << pos);
            }
        }
    }

    /* 2) 恢复原状：4 行全部拉高 */
    row_out(MK_ROW_NUM);

    now = (u16)SysTick_Get();

    /* 3) 记录发生变化的键的时刻 */
    diff = (u16)(s_raw ^ rawNow);
    if (diff != 0)
    {
        for (pos = 0; pos < MK_KEY_COUNT; pos++)
        {
            if ((diff >> pos) & 1)
            {
                s_changeMs[pos] = now;
            }
        }
        s_raw = rawNow;
    }

    /* 4) 判定稳定态并产生事件 */
    for (pos = 0; pos < MK_KEY_COUNT; pos++)
    {
        u8 cur = (u8)((rawNow >> pos) & 1);
        u8 st  = (u8)((s_stable >> pos) & 1);

        if (cur != st)
        {
            if ((u16)(now - s_changeMs[pos]) >= MK_DEBOUNCE_MS)
            {
                if (cur)
                {
                    s_stable |= (u16)(1UL << pos);
                    MK_on_keyup((u8)(pos >> 2), (u8)(pos & 0x03));
                }
                else
                {
                    s_stable &= (u16)~(1UL << pos);
                    MK_on_keydown((u8)(pos >> 2), (u8)(pos & 0x03));
                }
            }
        }
    }
}
