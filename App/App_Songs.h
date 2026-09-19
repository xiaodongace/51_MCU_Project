/*
 * App_Songs.h - 三首闹铃曲的音符表
 *
 * 音符格式：{音高, 时长(ms)}
 *   音高取值 1..21，对应 Driver/Buzzer.h 的 FREQS 表：
 *     1..7   = 低八度（523 ~ 988 Hz）
 *     8..14  = 中八度（1047 ~ 1976 Hz）
 *     15..21 = 高八度（2093 ~ 3951 Hz）
 *   0 表示休止。
 *
 * 为什么时长直接写毫秒而不写"几分音符"：
 *   引擎只看 g_sysTick 的差值，毫秒最直观，改速度也只要改一个宏。
 *   第一首《两只老虎》的音符序列照抄教材（v3.1 Driver/Buzzer.c 的 Buzzer_2tiger），
 *   只是把"拍数"换算成了毫秒。
 */
#ifndef __APP_SONGS_H
#define __APP_SONGS_H

#include "Config.h"

typedef struct
{
    u8  tone;       /* 1..21，0 = 休止 */
    u16 ms;         /* 持续毫秒 */
} Note_t;

/* 八度前缀，与 Driver/Buzzer.h 的 FREQS 表对应 */
#define L1 1
#define L2 2
#define L3 3
#define L4 4
#define L5 5
#define L6 6
#define L7 7
#define M1 8
#define M2 9
#define M3 10
#define M4 11
#define M5 12
#define M6 13
#define M7 14
#define H1 15
#define H2 16
#define H3 17
#define H4 18
#define H5 19
#define H6 20
#define H7 21
#define REST 0

/* 基本时值。改 BASE_Q 就能整体调快慢。 */
#define BASE_Q      400         /* 四分音符 */
#define BASE_E      200         /* 八分音符 */
#define BASE_QD     600         /* 附点四分音符 */
#define BASE_H      800         /* 二分音符 */

/* 第 idx 个音符；越界返回 tone=0, ms=0 */
void Songs_Get(u8 songId, u16 idx, u8 *tone, u16 *ms);

/* 曲子长度（音符个数）；songId 非法返回 0 */
u16 Songs_Length(u8 songId);

#endif
