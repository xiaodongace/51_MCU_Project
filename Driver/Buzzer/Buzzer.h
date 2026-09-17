#ifndef __BUZZER_H__
#define __BUZZER_H__
#include "App_Public.h"

// 任务循环周期(RTX tick 数),与 App_Buzzer.c 里的 os_wait2 保持一致
#define BUZZER_LOOP_TICKS 2

// 音量(定义在 Buzzer.c)
extern char Volume;
// 第几首(定义在 Buzzer.c)
extern char Song;

void Buzzer_Init(void);

u8 Buzzer_IsPlaying(void);
void Buzzer_Refresh(void);
void Buzzer_Play_Pause(u8 playing);
void Buzzer_NextSong(void);
void Buzzer_Tick(void);
void Buzzer_Clear(void);

#endif
