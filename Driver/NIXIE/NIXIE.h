#ifndef __NIXIE_H__
#define __NIXIE_H__

#include "config.h"
#include "App_Public.h"
#include "Timers.h"

#define	NIXIE_DI	P44	// 数据输入
#define	NIXIE_SCK	P42	// 移位寄存器
#define	NIXIE_RCK	P43	// 锁存寄存器

#define NIXIE_PIN_INIT() {    P4M0 &= ~0x1c; P4M1 &= ~0x1c; }

// 初始化
void Nixie_init();

void Nixie_Refresh(void);
void Nixie_SetNumber(u32 number);

// 在某一位显示数字
void Nixie_show(num, idx);

// 显示函数
void Nixie_display(num, idx);

// 扫描
void Nixie_Scan2ms();

void Nixie_task();

// 走马灯数码管
void Nixie_Run();

// 关闭数码管显示
void Nixie_Close();

/* 时钟递增 */
void Clock_Update();

extern volatile u8 nixie_digits[8];
extern volatile u8 date_page[8];
extern volatile u8 time_page[8];

void Nixie_SetDigits(u8 *digits);
/* 更新时间显示内容 */
void Update_Time_Page();

#endif

