/*
 * Buzzer.h - 无源蜂鸣器（P0.0，PWMB / PWM5）
 *
 * 真值来源：v3.1 Driver/Buzzer.c（真机跑通），做两处规范化（见 Buzzer.c）
 *   引脚 P0.0，PWM5_SW(PWM5_SW_P00)，PWMB 组
 *   改频率 = 改音高，改占空比 = 改响度
 */
#ifndef __BUZZER_H
#define __BUZZER_H

#include "Config.h"

sbit BUZZER = P0 ^ 0;

/* 音阶表长度：3 个八度 x 7 个音 = 21 */
#define BUZZER_TONE_MAX     21

/*
 * 最大响度对应的占空比（百分比）。
 *
 * v3.1 用的是 2%，江文聪老师封装驱动用的是 50%。
 * 《04-M1分工规格》G 角色要求"音量 0–10 可调、50% 最响"，所以本工程取 50%。
 * 实机若偏响或偏轻，只改这一个宏，别改算式。
 */
#define BUZZER_DUTY_MAX_PCT  50

/* 音量的满档值：旋钮给 0..10，10 对应 BUZZER_DUTY_MAX_PCT */
#define BUZZER_VOLUME_FULL   100  /* 音量改为百分比 0..100（第 7 点）*/

/*
 * 蜂鸣器总开关。
 * 0 = Buzzer_Play() 直接不发声（用户 2026-09-18 要求"先把蜂鸣器禁掉，
 *     蜂鸣器放在最后再处理"）。
 * 只静音，不影响任何事件流程与其它逻辑 —— 按键、闹钟、番茄钟照常工作，
 * 只是听不到声音。改回 1 即恢复。
 */
/* 【2026-09-20 · 用户要求"把蜂鸣器关闭，就是按键按下的声音关闭"】
 *
 * 0 = 整条蜂鸣器通道静音。Buzzer_Play() 整个函数体被 #if 包着，
 * 关闭时直接 Buzzer_Stop()（关 CC5E + 引脚拉低），**不做任何 PWM 操作**。
 *
 * 覆盖范围（已核对：全工程没有绕过 Buzzer_Play 直接碰 PWM5 的地方）：
 *   · 全部按键提示音（20 处 Music_Beep 调用点）—— 这正是用户要关的
 *   · 闹钟/番茄钟的曲目播放（Music_Play / Music_PlayLoop）
 *   · 设置页的 100ms 确认提示音
 *
 * 【副作用 · 必须知道】闹钟响铃时**也不会发声**了（界面照常回到响铃页）。
 * 用户要的是"按键音关闭"；若只想关按键音而保留闹钟，那是另一种改法
 * （只掐 Music_Beep 的提示音路径，不动 Music_Play），本条注释下方有说明。
 *
 * 改回 1 即可完整恢复（音量算式仍是百分比 0..100，不受影响）。 */
#define BUZZER_ENABLE        1    /* 【2026-09-21】用户要求重新开启蜂鸣器 */

/* 上电安全电平：只写 IO，可在 EAXSFR() 之前调用 */
void Buzzer_SafeLevel(void);

/* 初始化：P0.0 推挽 + 配 PWM5/PWMB + 关闭输出 */
void Buzzer_Init(void);

/*
 * 按指定频率发声，volume 取 0..10。
 * volume = 0 等价于 Buzzer_Stop()。
 * 内部顺序（《04》要求）：先关输出 -> 改频率 -> 改占空比 -> 再开输出，避免换音时"啪"的爆音。
 */
void Buzzer_Play(u16 hz_value, u8 volume);

/* 停止发声 */
void Buzzer_Stop(void);

/* 查询蜂鸣器总开关是否打开（= BUZZER_ENABLE）。
 * 给 App 层用：蜂鸣器关掉时，"按 KEY2 确认"的 100ms 反馈要退回用马达给，
 * 否则用户按了没有任何回应。驱动层不做策略，只把这个事实暴露出来。 */
u8 Buzzer_IsEnabled(void);

/* 音阶索引 1..21 -> 频率（Hz）。0 或越界返回 0 */
u16 Buzzer_Freq(u8 tone);

/* 提示音默认频率（C5） */
#define BUZZER_BEEP_HZ      1047

#endif
