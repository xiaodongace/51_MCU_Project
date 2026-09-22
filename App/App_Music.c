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

/* 【2026-09-22 断音状态】见 play_current() 里的说明。
 * 每个音符分两段：先"响" s_noteMs，再"静音" s_gapMs，然后才进下一个音符。 */
static u8  s_gapPhase = 0;          /* 1 = 正在断音（静音）那一段 */
static u16 s_gapMs    = 0;          /* 本音符的断音时长 */

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

    /* 【2026-09-22 修用户报的"音调慢的有拖音"】
     *
     * 根因：原来音符之间**没有任何断音** —— 上一个音一直响到下一个音开始的那一瞬间。
     * 方波蜂鸣器没有衰减，于是：
     *   · **同音相邻**的音符会被连成一个长音。最典型的是《两只老虎》
     *     的 "3 1 | 1 2 3"：句尾的 1 和下一句开头的 1 合成一个两拍长音，
     *     乐句的分界消失，节奏整个塌掉；
     *   · 不同音之间也没有"字头"，听着黏成一片。
     * -> 主观感受就是"音调慢 / 拖音"（其实是**没有断音**，不是速度问题）。
     *
     * 解法很朴素：**每个音符只响 7/8，留 1/8 静音当断音**。
     * 两段加起来正好还是 ms -> 不影响速度，只是把音符切开、给出字头。
     * 休止符本来就是静音，不用再切。 */
    if (tone == REST)
    {
        s_gapMs  = 0;
        s_noteMs = ms;
    }
    else
    {
        s_gapMs  = (u16)(ms / 8);
        s_noteMs = (u16)(ms - s_gapMs);
    }

    s_gapPhase  = 0;
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
    s_gapPhase = 0;

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
    s_gapPhase = 0;
    s_gapMs    = 0;
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
        /* 先走"断音"那一段（静音一小会儿），再起下一个音符。
         * 少了这一步，相邻音符会糊在一起 —— 那就是用户听到的"拖音"。 */
        if (!s_gapPhase && s_gapMs != 0)
        {
            s_gapPhase  = 1;
            s_noteMs    = s_gapMs;
            s_noteStart = SysTick_Get();
            Buzzer_Stop();
            return;
        }

        s_gapPhase = 0;
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
