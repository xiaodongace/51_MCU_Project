/*
 * NixieScan.h - 数码管 1ms 扫描（Timer2 中断驱动）
 *
 * 为什么需要这个文件（这是本工程相对文档的一处显式偏离，理由如下）：
 *
 *   《02-技术方案》6.1 写的是"T0 渲染任务每 2 毫秒刷一位数码管"。
 *   但 RTX51 Tiny 的心跳是 5ms（OS/Conf_tny.A51 配置），os_wait2 的最小等待
 *   单位就是 5ms —— 任务最快也只能 5ms 醒一次，2ms 在 RTX51 下不可实现。
 *   若用任务扫描：8 位 x 5ms = 一帧 40ms = 25Hz（v3.1 的 App_Digital.c 里
 *   就写着 `os_wait2(K_TMO, 1); // 5ms -> 40ms (25Hz)`）。
 *
 *   本工程改用 Timer2 1kHz 中断里刷一位：一帧 8ms = 125Hz。
 *   这是 v3.1 在**同一块板**上真机跑通的方案，符合规范第三条"逐字对齐例程"。
 *
 *   代价：占用了 Timer2。《02》原本把 Timer2 留给超声波（M3），
 *        而《02》第八节又说超声波只能"轮询计时"（它只支持下降沿触发），
 *        轮询可以直接挂在 Timer3 的 g_sysTick 上做差值，不冲突。
 *
 * 中断里的代码必须极短：每次只刷一位，32 次移位 + 一次锁存，约 20us，
 * 占 1ms 的 2%（《04》D 角色要求"单次扫描一位控制在 200 微秒内"）。
 */
#ifndef __NIXIESCAN_H
#define __NIXIESCAN_H

#include "Config.h"
#include "NIXIE.h"

#define NIXIE_DIGITS        8

/* LED_TABLE 里可用的特殊字符下标（照抄 v3.1 Driver/NIXIE.c 的注释） */
#define NIXIE_CH_BLANK      0xFF    /* 传 0xFF 表示这一位不显示：NIXIE_show(0xFF, 0) 全灭 */
#define NIXIE_CH_DOT        20      /* 小数点 */
#define NIXIE_CH_DASH       21      /* 中间横杠，用作时钟分隔符 */

/*
 * 位序方向开关。
 * 物理上 8 位数码管的第 0 位在左还是在右，取决于接线；
 * v3.1 的 App_Digital.c 用 NIXIE_display(i+1, i) 正序扫描，本工程沿用正序。
 * 如果实机上看到时钟是"倒着的"（例如 12-34-56 显示成 65-43-21），
 * 把这里改成 1 即可，不用动别的地方。
 */
#define NIXIE_POS_REVERSED  0

/* 初始化：配数码管 IO + 启动 Timer2 1kHz 中断 */
void Nixie_ScanInit(void);

/* 停止扫描（关 Timer2），并把数码管全灭 */
void Nixie_ScanStop(void);

/* 设置某一位的显示内容（numId 是 LED_TABLE 的下标；位序按 NIXIE_POS_REVERSED 处理） */
void Nixie_SetDigit(u8 pos, u8 numId);

/* 用 8 位内容一次性设置整屏（buf[0] 是最左边那位） */
void Nixie_SetBuffer(const u8 *buf);

/* 显示时分秒：布局为 H H - M M - S S */
void Nixie_SetTime(u8 hour, u8 minute, u8 second);

/* 全灭 */
void Nixie_Clear(void);

/* 由 Timer2 中断调用：刷一位 */
void Nixie_Scan1ms(void);

#endif
