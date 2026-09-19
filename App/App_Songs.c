/*
 * App_Songs.c - 三首闹铃曲
 *
 * 曲目数据全部放 code 区（程序 Flash），不占 RAM（《04》G 角色注意事项：
 * "曲子数据必须放在程序区，放内存会爆"）。
 */
#include "App_Songs.h"

/*------------------------------------------------------------------------
 * 1. 两只老虎
 *    简谱：1 2 3 1 | 1 2 3 1 | 3 4 5 - | 3 4 5 - |
 *          5 6 5 4 3 1 | 5 6 5 4 3 1 | 1 5 1 - | 1 5 1 -
 *    音符序列与拍数照抄教材，拍数换算成毫秒。
 *    中八度演奏（教材用的就是 hz[] 里的 1047..1976 那一组）。
 *------------------------------------------------------------------------*/
static Note_t code s_tiger[] =
{
    {M1, BASE_Q}, {M2, BASE_Q}, {M3, BASE_Q}, {M1, BASE_Q},
    {M1, BASE_Q}, {M2, BASE_Q}, {M3, BASE_Q}, {M1, BASE_Q},
    {M3, BASE_Q}, {M4, BASE_Q}, {M5, BASE_H},
    {M3, BASE_Q}, {M4, BASE_Q}, {M5, BASE_H},
    {M5, BASE_QD}, {M6, BASE_E}, {M5, BASE_QD}, {M4, BASE_E}, {M3, BASE_Q}, {M1, BASE_Q},
    {M5, BASE_QD}, {M6, BASE_E}, {M5, BASE_QD}, {M4, BASE_E}, {M3, BASE_Q}, {M1, BASE_Q},
    {M1, BASE_Q}, {M5, BASE_Q}, {M1, BASE_H},
    {M1, BASE_Q}, {M5, BASE_Q}, {M1, BASE_H},
};

/*------------------------------------------------------------------------
 * 2. 小星星
 *    简谱：1 1 5 5 6 6 5 - | 4 4 3 3 2 2 1 - |
 *          5 5 4 4 3 3 2 - | 5 5 4 4 3 3 2 - |
 *------------------------------------------------------------------------*/
static Note_t code s_star[] =
{
    {M1, BASE_Q}, {M1, BASE_Q}, {M5, BASE_Q}, {M5, BASE_Q}, {M6, BASE_Q}, {M6, BASE_Q}, {M5, BASE_H},
    {M4, BASE_Q}, {M4, BASE_Q}, {M3, BASE_Q}, {M3, BASE_Q}, {M2, BASE_Q}, {M2, BASE_Q}, {M1, BASE_H},
    {M5, BASE_Q}, {M5, BASE_Q}, {M4, BASE_Q}, {M4, BASE_Q}, {M3, BASE_Q}, {M3, BASE_Q}, {M2, BASE_H},
    {M5, BASE_Q}, {M5, BASE_Q}, {M4, BASE_Q}, {M4, BASE_Q}, {M3, BASE_Q}, {M3, BASE_Q}, {M2, BASE_H},
};

/*------------------------------------------------------------------------
 * 3. 生日快乐
 *    简谱：5 5 6 5 1' 7 - | 5 5 6 5 2' 1' - |
 *          5 5 5' 3 1 7 6 | 4 4 3 1 2 1 -
 *    1' / 2' / 5' 是高八度。
 *------------------------------------------------------------------------*/
static Note_t code s_birthday[] =
{
    {M5, BASE_Q}, {M5, BASE_Q}, {M6, BASE_Q}, {M5, BASE_Q}, {H1, BASE_Q}, {M7, BASE_H},
    {M5, BASE_Q}, {M5, BASE_Q}, {M6, BASE_Q}, {M5, BASE_Q}, {H2, BASE_Q}, {H1, BASE_H},
    {M5, BASE_Q}, {M5, BASE_Q}, {H5, BASE_Q}, {M3, BASE_Q}, {M1, BASE_Q}, {M7, BASE_Q}, {M6, BASE_H},
    {M4, BASE_Q}, {M4, BASE_Q}, {M3, BASE_Q}, {M1, BASE_Q}, {M2, BASE_Q}, {M1, BASE_H},
};

u16 Songs_Length(u8 songId)
{
    switch (songId)
    {
    case 1: return (u16)(sizeof(s_tiger) / sizeof(s_tiger[0]));
    case 2: return (u16)(sizeof(s_star) / sizeof(s_star[0]));
    case 3: return (u16)(sizeof(s_birthday) / sizeof(s_birthday[0]));
    default: return 0;
    }
}

void Songs_Get(u8 songId, u16 idx, u8 *tone, u16 *ms)
{
    Note_t code *p;

    *tone = 0;
    *ms   = 0;

    switch (songId)
    {
    case 1: p = s_tiger;    break;
    case 2: p = s_star;     break;
    case 3: p = s_birthday; break;
    default: return;
    }

    if (idx >= Songs_Length(songId))
    {
        return;
    }

    *tone = p[idx].tone;
    *ms   = p[idx].ms;
}
