/*
 * App_Music.h - 非阻塞音乐引擎
 *
 * 设计目标（《04-M1分工规格》G 角色 G2）：放曲子时不挡其他任务，能随时停止。
 *
 * 做法：曲子存成"音符表"（每个音符记音高 + 时长）；
 *       TASK_MUSIC 每次被调度时看一眼 g_sysTick，判断当前音符播够了没有，
 *       够了就换下一个。整个过程不等待、不阻塞 —— 放音乐时按键照样灵。
 *
 * 音高用简谱数字 1..7（低/中/高八度用 L/N/H 前缀，见 Driver/Buzzer.h 的频率表）。
 * 运行时立刻把频率交给 Driver/Buzzer.c，不在本模块里碰寄存器。
 */
#ifndef __APP_MUSIC_H
#define __APP_MUSIC_H

#include "App_Public.h"

/* 曲目编号：对外统一用 1..3（0 表示"不播"） */
#define SONG_NONE       0
#define SONG_TIGER      1       /* 两只老虎 */
#define SONG_STAR       2       /* 小星星 */
#define SONG_BIRTHDAY   3       /* 生日快乐 */
#define SONG_COUNT      3

/*
 * 开始播放。
 *   songId : 1..SONG_COUNT
 *   volume : 0..10，0 等于停止
 */
void Music_Play(u8 songId, u8 volume);

/* 停止播放（马达不受本函数影响，由 App_Motor 单独管） */
void Music_Stop(void);

/* 是否正在播放 */
u8 Music_IsPlaying(void);

/* 当前曲目编号，1..SONG_COUNT；没在播返回 SONG_NONE */
u8 Music_CurrentSong(void);

/* 当前音量 0..10 */
u8 Music_Volume(void);

/*
 * 非阻塞推进：拍子到了就换下一个音符。
 * 由 TASK_MUSIC 调用，调用频率越高节奏越准（本工程每 5ms 一次，
 * 而最短音符是 8 分音符 = 250ms，所以精度绰绰有余）。
 */
void Music_Tick(void);

/*
 * 短促提示音（页面切换、确认、保存时用）。
 * 非阻塞：立刻发声，到点在 Music_Tick 里关掉，绝不 delay。
 * 正在放曲子时会被忽略（不打断音乐）。
 */
void Music_Beep(u16 ms);

/* 响铃用的循环播放（放完自动从头再来）。普通播放用 Music_Play。 */
void Music_PlayLoop(u8 songId, u8 volume);

#endif
