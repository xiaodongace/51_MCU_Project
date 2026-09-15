#include "Key.h"

/* 4个独立按键 */
#define KEY1 P51
#define KEY2 P52
#define KEY3 P53
#define KEY4 P54


/* 按键状态 */
#define DOWN    0
#define UP      1


/* 使用全部四个独立按键：KEY1、KEY2、KEY3、KEY4 */
#define KEY_INPUT_COUNT 4


/* 原始电平连续保持10 ms后 才确认为新的稳定状态 */
#define KEY_DEBOUNCE_MS 10


/*
 * 公共非阻塞消抖状态。
 * raw保存最近一次采样值
 * stable保存已确认状态
 * change_ms记录raw变化时刻
 * press_event锁存一次有效按下事件
 * 直到业务调用Key_GetPressEvent()取走
 */
static u8 key_raw_state[KEY_INPUT_COUNT];
static u8 key_stable_state[KEY_INPUT_COUNT];
static u16 key_change_ms[KEY_INPUT_COUNT];
static u8 key_press_event[KEY_INPUT_COUNT];


/* 配置按键GPIO为带上拉输入 仅供本模块内部使用 */
static void Key_GPIO_Config(void) {
    P5_MODE_IO_PU(GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4);
}


/* 读取指定按键当前电平 */
static u8 get_key_value(u8 k) {
    switch (k) {
    case 0:
        return KEY1;
    case 1:
        return KEY2;
    case 2:
        return KEY3;
    case 3:
        return KEY4;
    default:
        return 0;
    }
}


/*
 * 根据上电时的真实按键电平初始化公共消抖状态
 * 初始原始状态与稳定状态保持一致
 * 避免把上电时的电平误判为一次按下事件
 */
static void Key_EventInit(void) {
    u8 i;
    u16 now = Timers_GetSystemMs();

    /* 三个按键分别保存自己的状态和最近变化时间互不影响 */
    for (i = 0; i < KEY_INPUT_COUNT; i++) {
        key_raw_state[i] = (get_key_value(i) == DOWN);
        key_stable_state[i] = key_raw_state[i];
        key_change_ms[i] = now;
        key_press_event[i] = 0;
    }
}


/*
 * 初始化按键GPIO及公共非阻塞消抖状态
 */
void Key_Init(void) {
    Key_GPIO_Config();
    Key_EventInit();
}


/*
 * 扫描所有按键并用系统时间执行非阻塞消抖
 * 函数只更新按键状态并产生事件
 */
void Key_Scan(void) {
    u8 i;
    u8 raw_state;
    u16 now = Timers_GetSystemMs();

    /* 每个按键都有独立的原始状态、稳定状态和消抖计时起点 */
    for (i = 0; i < KEY_INPUT_COUNT; i++) {
        raw_state = (get_key_value(i) == DOWN);

        if (raw_state != key_raw_state[i]) {
            /* 原始电平刚变化 可能仍在抖动 只记录状态和变化时刻 */
            key_raw_state[i] = raw_state;
            key_change_ms[i] = now;
        }
        else if (raw_state != key_stable_state[i]) {
            /* 原始电平持续不变且与稳定状态不同 检查是否已稳定满10 ms */
            if (now - key_change_ms[i] >= KEY_DEBOUNCE_MS) {
                key_stable_state[i] = raw_state;

                /* 只在稳定地变为按下时产生事件 松开仅更新稳定状态 */
                if (key_stable_state[i] != 0) {
                    key_press_event[i] = 1;
                }
            }
        }
    }
}


/*
 * 读取并消费指定按键的一次性按下事件
 * 返回1表示取得一个新事件
 * 事件读取后立即清零
 * 按住按键不会重复触发
 */
u8 Key_GetPressEvent(u8 key_index) {
    /* 非法编号不访问数组 也不产生按键事件 */
    if (key_index >= KEY_INPUT_COUNT) {
        return 0;
    }

    /* 没有待处理事件时立即返回 保持任务函数快速、非阻塞 */
    if (key_press_event[key_index] == 0) {
        return 0;
    }

    /* 将本次事件交给调用者 并清零以等待下一次完整按下 */
    key_press_event[key_index] = 0;
    return 1;
}


/*
 * 查询按键当前是否被按下
 * 非零为按下 0 为松开
 */
u8 Key_IsPressed(u8 key_index) {
    if (key_index >= KEY_INPUT_COUNT) {
        return 0; /* 非法按键编号不访问状态数组 */
    }

    return (get_key_value(key_index) == DOWN);
}

