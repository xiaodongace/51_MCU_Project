/*
 * App_Music.c - 非阻塞音乐引擎 + TASK_MUSIC
 *
 * 关键点：整个推进过程**不等待**（《04》G 角色 G2："放曲子时不挡其他任务"）。
 *   Music_Tick() 只是"看表判断该不该换音符"，从不调用任何延时。
 *
 * TASK_MUSIC 每 5ms 跑一次：
 *   · 每次：Music_Tick()  —— 音符时长最短 200ms，5ms 精度绰绰有余
 *   · 每 20 次（=100ms）：Alarm_TickSunrise() —— 日出渐亮每 100ms 调一级
 */
#include "App_Music.h"
#include "Motor.h"

#include "App_Songs.h"
#include "Buzzer.h"
#include "App_Alarm.h"

/*========================================================================
 *                              内部状态
 *========================================================================*/

static u8  s_song    = SONG_NONE;   /* 当前曲目 1..3 */
static u8  s_volume  = 0;           /* 0..100（百分比，第 7 点）*/
static u16 s_idx     = 0;           /* 当前音符下标 */
static u16 s_noteMs  = 0;           /* 当前音符应持续多少毫秒 */
static u32 s_noteStart = 0;         /* 当前音符开始时的系统时钟 */
static u8  s_playing = 0;
static u8  s_loop    = 0;           /* 1 = 放完从头再来（响铃用） */

/*========================================================================
 *                              内部函数
 *========================================================================*/

/* 把第 s_idx 个音符交给蜂鸣器，并记下起始时刻 */
static void play_current(void)
{
    u8  tone;
    u16 ms;

    Songs_Get(s_song, s_idx, &tone, &ms);

    if (ms == 0)
    {
        /* 走到曲子末尾 */
        if (s_loop)
        {
            s_idx = 0;
            Songs_Get(s_song, s_idx, &tone, &ms);
        }
        else
        {
            Music_Stop();
            return;
        }
    }

    s_noteMs    = ms;
    s_noteStart = g_sysTick;

    if (tone == REST)
    {
        /* 休止符：只停声，不改变"正在播放"的状态 */
        Buzzer_Stop();
    }
    else
    {
        Buzzer_Play(Buzzer_Freq(tone), s_volume);
    }
}

static void music_start(u8 songId, u8 volume, u8 loop)
{
    if (songId < 1 || songId > SONG_COUNT || volume == 0)
    {
        Music_Stop();
        return;
    }

    s_song   = songId;
    s_volume = volume;
    s_idx    = 0;
    s_loop   = loop;
    s_playing = 1;

    play_current();
}

/*========================================================================
 *                          短促提示音（非阻塞）
 *========================================================================*/

static u8  s_beeping   = 0;
static u32 s_beepStart = 0;
static u16 s_beepMs    = 0;

void Music_Beep(u16 ms)
{
    u8 vol;

    /* 正在放曲子时不打岔 —— 提示音只是辅助，不该盖住闹铃 */
    if (s_playing)
    {
        return;
    }

    /* 音量 0 时也给一个最低可闻档，否则用户会以为"没响" */
    vol = g_settings.volume;
    if (vol < 3)
    {
        vol = 3;
    }

    Buzzer_Play(BUZZER_BEEP_HZ, vol);

    s_beepStart = g_sysTick;
    s_beepMs    = ms;
    s_beeping   = 1;
}

/*========================================================================
 *                              对外接口
 *========================================================================*/

void Music_Play(u8 songId, u8 volume)
{
    s_beeping = 0;      /* 起曲子时把提示音收掉 */
    music_start(songId, volume, 0);
}

void Music_PlayLoop(u8 songId, u8 volume)
{
    music_start(songId, volume, 1);
}

void Music_Stop(void)
{
    Buzzer_Stop();

    s_beeping = 0;
    s_playing = 0;
    s_loop    = 0;
    s_song    = SONG_NONE;
    s_idx     = 0;
    s_noteMs  = 0;
}

u8 Music_IsPlaying(void)
{
    return s_playing;
}

u8 Music_CurrentSong(void)
{
    return s_song;
}

u8 Music_Volume(void)
{
    return s_volume;
}

/*
 * 非阻塞推进：拍子到了就换下一个音符。
 * 注意这里用 SysTick_Elapsed 而不是 os_wait2 —— 时间推进不依赖等待（《02》4.1）。
 */
void Music_Tick(void)
{
    /* 提示音优先：到点就关掉，期间不让歌曲逻辑抢蜂鸣器 */
    if (s_beeping)
    {
        if (SysTick_Elapsed(s_beepStart) >= (u32)s_beepMs)
        {
            s_beeping = 0;
            Buzzer_Stop();
        }
        return;
    }

    if (!s_playing)
    {
        return;
    }

    if (SysTick_Elapsed(s_noteStart) >= (u32)s_noteMs)
    {
        s_idx++;
        play_current();
    }
}

/*========================================================================
 *                    TASK_MUSIC：5ms 一拍
 *========================================================================*/

void task_music(void) _task_ TASK_MUSIC
{
    u8 sunriseDiv = 0;

    /* 上电先静音 */
    Buzzer_Stop();

    while (1)
    {
        /* 每次：推进音符 */
        Music_Tick();

        /* 【第 7 点】震动计时的心跳。
         * 必须和 Music_Tick 并排放在这个 5ms 任务里 —— Motor_Vibrate(100,...)
         * 就是按"5ms 一拍"换算成 20 拍自动收尾的。 */
        Motor_Tick();

        /* 每 20 x 5ms = 100ms：调一次日出唤醒的亮度 */
        sunriseDiv++;
        if (sunriseDiv >= 20)
        {
            sunriseDiv = 0;
            Alarm_TickSunrise();
        }

        os_wait2(K_TMO, 1);     /* 1 x 5ms */
    }
}
