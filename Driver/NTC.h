/*
 * NTC.h - NTC 热敏电阻测温（P0.4，ADC 通道 12）
 *
 * 真值来源：
 *   v3.1 Driver/NTC.h  -> NTC_GPIO_PORT = GPIO_P0，NTC_GPIO_PIN = GPIO_Pin_4，NTC_ADC_CH = ADC_CH12
 *   v3.1 Driver/NTC.c  -> 整张"电阻-温度"对照表（temp_table），查表法
 *   《04-M1分工规格》F 角色 -> "温度用查表法，不要用对数公式（太占空间）"
 *
 * 相对 v3.1 的改动（按规范显式列出）：
 *   v3.1 的 NTC_get_temperature() 用 float 算电压和电阻：
 *       ntc_V = adc_value * 2.5 / 4096;      ntc_R = ntc_V * 10 / (3.3 - ntc_V);
 *   本工程全部改整数（毫伏与"百分之一千欧"），理由同 DHT11：去浮点库。
 *   查表用的是 v3.1 原表，未做任何改动。
 */
#ifndef __NTC_H
#define __NTC_H

#include "Config.h"

/* 把未来可能改变的引脚、通道提到头文件（照抄 v3.1），换板子只改这里 */
#define NTC_GPIO_PORT       GPIO_P0
#define NTC_GPIO_PIN        GPIO_Pin_4
#define NTC_ADC_CH          ADC_CH12

/* ADC 参考电压 2.5V（板上 ADC_VRef+ 接 2.5V），分压上拉电源 3.3V
 * 这两个数是 v3.1 Driver/NTC.c 里写死的，不要凭空改 */
#define NTC_VREF_MV         2500L
#define NTC_VCC_MV          3300L
#define NTC_ADC_FULL        4096L
/* 分压电阻 10 kΩ */
#define NTC_SERIES_KOHM     10L

/* 初始化：P0.4 配置为高阻输入 */
void NTC_Init(void);

/*
 * 读温度，单位 x10（253 表示 25.3℃）。
 * 内部对最近 4 次结果做平均，抑制跳变（《04》F1 要求"带一点滤波"）。
 * 温度来自查表，只有整数精度，x10 的单位保持与 DHT11 一致，方便一起显示。
 */
s16 NTC_GetTempX10(void);

#endif
