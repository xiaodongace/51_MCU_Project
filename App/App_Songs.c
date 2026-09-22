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
 * 3. 马兰开花二十一（跳皮筋 / 拍手童谣）
 *
 *    歌词与断句：
 *      小皮球，香蕉梨，马兰开花二十一
 *      二五六，二五七，二八二九三十一
 *
 *    每句的音符数严格对着字数排（六 / 七 / 六 / 七）：
 *      小皮球，香蕉梨      6 字 -> 6 个音
 *      马兰开花二十一      7 字 -> 7 个音
 *      二五六，二五七      6 字 -> 6 个音
 *      二八二九三十一      7 字 -> 7 个音
 *    四句都落在中八度，末句收在主音（M1），是儿歌常见的走法。
 *
 *    【必须说明】这是**我按童谣的节奏型和歌词断句编配的简易版**，
 *    不是我抄来的标准旋律（这首童谣各地唱法不一，没有唯一版本）。
 *    如果你手上有具体简谱（比如 5 5 3 5 6 5），发我，我按你的改 ——
 *    只要替换下面这一张表就行，引擎不用动。
 *------------------------------------------------------------------------*/
static Note_t code s_malan[] =
{
    /* 小皮球，香蕉梨 */
    {M5, BASE_Q}, {M5, BASE_E}, {M3, BASE_E}, {M5, BASE_Q}, {M6, BASE_E}, {M5, BASE_E},
    /* 马兰开花二十一 */
    {M6, BASE_Q}, {M6, BASE_E}, {M5, BASE_E}, {M3, BASE_Q}, {M3, BASE_E}, {M2, BASE_E}, {M1, BASE_Q},
    /* 二五六，二五七 */
    {M3, BASE_E}, {M3, BASE_E}, {M5, BASE_Q}, {M3, BASE_E}, {M3, BASE_E}, {M2, BASE_Q},
    /* 二八二九三十一 */
    {M3, BASE_E}, {M5, BASE_E}, {M6, BASE_Q}, {M5, BASE_E}, {M3, BASE_E}, {M2, BASE_E}, {M1, BASE_Q},
};

u16 Songs_Length(u8 songId)
{
    switch (songId)
    {
    case 1: return (u16)(sizeof(s_tiger) / sizeof(s_tiger[0]));
    case 2: return (u16)(sizeof(s_star) / sizeof(s_star[0]));
    case 3: return (u16)(sizeof(s_malan) / sizeof(s_malan[0]));
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
    case 3: p = s_malan; break;
    default: return;
    }

    if (idx >= Songs_Length(songId))
    {
        return;
    }

    *tone = p[idx].tone;
    *ms   = p[idx].ms;
}
