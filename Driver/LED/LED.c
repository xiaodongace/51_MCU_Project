#include "LED.h"

void LED_GPIO() {
    P4_MODE_IO_PU(GPIO_Pin_5);
    P2_MODE_OUT_PP(GPIO_Pin_7 | GPIO_Pin_6 | GPIO_Pin_3 | GPIO_Pin_2 | GPIO_Pin_1 | GPIO_Pin_0);
    P1_MODE_OUT_PP(GPIO_Pin_5 | GPIO_Pin_4);
}

void LED_Init(void) {
    LED_GPIO();
    LED_SW = 0;     // LED 模块电源使能(0=开,与 V0.2 一致;只有 8 个灯通道是低电平点亮)
    LED1 = LED2 = LED3 = LED4 = LED5 = LED6 = LED7 = LED8 = 1;   // 1 = 灭
}

// 随机点亮 2-6 个 LED 灯
// LED1~LED8 是分布在 P1/P2 上的 sbit,sbit 不能按下标访问,
// 所以用"位掩码表"描述每个灯在端口上的位置,再整体改写端口
static u8 code P2_LED_MASK[8] = {0x80, 0x40, 0x00, 0x00, 0x08, 0x04, 0x02, 0x01}; // LED1 LED2 ... LED5 LED6 LED7 LED8
static u8 code P1_LED_MASK[8] = {0x00, 0x00, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00}; //           LED3 LED4

// 伪随机数发生器(线性同余法)
static u32 led_seed = 1;
static u8 LED_Rand(void) {
    led_seed = led_seed * 1103515245UL + 12345UL;
    return (u8)(led_seed >> 16);
}

// 全部熄灭(灯是低电平点亮,灭 = 把灯位全部置 1)
void LED_AllOff(void) {
    P2 |= 0xCF;                             // 置 P27 P26 P23 P22 P21 P20
    P1 |= 0x30;                             // 置 P15 P14
}

void LED_Random(void) {
    u8 idx[8] = {0, 1, 2, 3, 4, 5, 6, 7};   // 灯的编号池,洗牌后取前 count 个
    u8 count;
    u8 i;

    led_seed += Timers_GetSystemMs();       // 混入系统时间,避免每次上电序列相同
    count = 2 + LED_Rand() % 5;             // 随机 2~6 个

    LED_AllOff();                           // 先全灭

    // 部分洗牌:每轮从剩余编号里随机挑一个换到前面,保证点亮的灯不重复
    for (i = 0; i < count; i++) {
        u8 j = i + LED_Rand() % (8 - i);
        u8 t = idx[i];
        idx[i] = idx[j];
        idx[j] = t;

        // 0 为亮 1 为灭:把抽中的灯位清 0 即点亮
        P2 &= (u8)~P2_LED_MASK[idx[i]];
        P1 &= (u8)~P1_LED_MASK[idx[i]];
    }
}
