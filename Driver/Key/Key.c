#include "Key.h"

/* 四个独立按键对应的GPIO */
#define KEY1 P51
#define KEY2 P52
#define KEY3 P53
#define KEY4 P54

/* 按键有效电平 */
#define KEY_DOWN_LEVEL 0
#define KEY_UP_LEVEL 1

/* 独立按键数量 */
#define KEY_INPUT_COUNT 4

/* 按键消抖时间，单位为毫秒 */
#define KEY_DEBOUNCE_MS 10

/* 长按判定时间，单位为毫秒 */
#define KEY_LONG_PRESS_MS 800

/*
 * 最近一次读取到的原始按键状态
 * 1表示按下，0表示松开
 */
static u8 key_raw_state[KEY_INPUT_COUNT];

/*
 * 完成消抖后的稳定按键状态
 * 1表示按下，0表示松开
 */
static u8 key_stable_state[KEY_INPUT_COUNT];

/* 原始电平发生变化的时间 */
static u16 key_change_ms[KEY_INPUT_COUNT];

/* 按键完成稳定按下的时间 */
static u16 key_press_start_ms[KEY_INPUT_COUNT];

/* 普通按下事件 */
static u8 key_press_event[KEY_INPUT_COUNT];

/* 短按松开事件 */
static u8 key_short_event[KEY_INPUT_COUNT];

/* 长按事件 */
static u8 key_long_event[KEY_INPUT_COUNT];

/*
 * 是否已经产生过长按事件
 * 防止一直按住时重复产生长按
 */
static u8 key_long_reported[KEY_INPUT_COUNT];

/*
 * 配置四个独立按键为上拉输入
 */
static void Key_GPIO_Config(void) {
    P5_MODE_IO_PU(
        GPIO_Pin_1 |
        GPIO_Pin_2 |
        GPIO_Pin_3 |
        GPIO_Pin_4);
}

/*
 * 读取指定按键的GPIO电平
 */
static u8 Key_ReadLevel(u8 key_index) {
    switch (key_index) {
    case 0:
        return KEY1;

    case 1:
        return KEY2;

    case 2:
        return KEY3;

    case 3:
        return KEY4;

    default:
        return KEY_UP_LEVEL;
    }
}

/*
 * 初始化按键内部状态
 */
static void Key_EventInit(void) {
    u8 i;
    u16 now;

    /* 获取当前系统毫秒数 */
    now = Timers_GetSystemMs();

    for (i = 0; i < KEY_INPUT_COUNT; i++) {
        /*
         * 把低电平转换成逻辑按下状态
         */
        key_raw_state[i] =
            (Key_ReadLevel(i) == KEY_DOWN_LEVEL);

        key_stable_state[i] = key_raw_state[i];

        /* 初始化消抖计时 */
        key_change_ms[i] = now;

        /* 初始化长按计时 */
        key_press_start_ms[i] = now;

        /* 清除全部按键事件 */
        key_press_event[i] = 0;
        key_short_event[i] = 0;
        key_long_event[i] = 0;

        /*
         * 如果上电时按键已经按住，
         * 不把它识别成一次长按事件
         */
        if (key_stable_state[i])
            key_long_reported[i] = 1;
        else
            key_long_reported[i] = 0;
    }
}

/*
 * 初始化按键模块
 */
void Key_Init(void) {
    /* 配置按键GPIO */
    Key_GPIO_Config();

    /* 初始化按键状态 */
    Key_EventInit();
}

/*
 * 扫描全部按键并处理消抖、短按和长按
 */
void Key_Scan(void) {
    u8 i;
    u8 raw_state;
    u16 now;

    /* 获取当前系统毫秒数 */
    now = Timers_GetSystemMs();

    for (i = 0; i < KEY_INPUT_COUNT; i++) {
        /*
         * 把GPIO低电平转换为逻辑按下状态
         */
        raw_state =
            (Key_ReadLevel(i) == KEY_DOWN_LEVEL);

        /*
         * 原始电平发生变化，
         * 记录变化时间并重新开始消抖
         */
        if (raw_state != key_raw_state[i]) {
            key_raw_state[i] = raw_state;
            key_change_ms[i] = now;
        }
        /*
         * 原始状态保持稳定，
         * 但还没有更新到稳定状态
         */
        else if (raw_state != key_stable_state[i]) {
            /*
             * 电平稳定达到消抖时间，
             * 确认本次状态变化
             */
            if ((u16)(now - key_change_ms[i]) >=
                KEY_DEBOUNCE_MS) {
                key_stable_state[i] = raw_state;

                /*
                 * 确认按键按下
                 */
                if (key_stable_state[i]) {
                    /* 产生一次普通按下事件 */
                    key_press_event[i] = 1;

                    /* 记录稳定按下的时间 */
                    key_press_start_ms[i] = now;

                    /* 本次按键还没有产生长按事件 */
                    key_long_reported[i] = 0;

                    /* 清除上一次可能没有读取的事件 */
                    key_short_event[i] = 0;
                    key_long_event[i] = 0;
                }
                /*
                 * 确认按键松开
                 */
                else {
                    /*
                     * 如果本次没有产生过长按，
                     * 松开时产生一次短按事件
                     */
                    if (key_long_reported[i] == 0)
                        key_short_event[i] = 1;

                    /*
                     * 为下一次按键动作重新准备
                     */
                    key_long_reported[i] = 0;
                }
            }
        }

        /*
         * 按键保持按下，并且还没有产生长按事件
         */
        if (key_stable_state[i] &&
            key_long_reported[i] == 0) {
            /*
             * 达到长按时间后产生一次长按事件
             */
            if ((u16)(now - key_press_start_ms[i]) >=
                KEY_LONG_PRESS_MS) {
                key_long_event[i] = 1;
                key_long_reported[i] = 1;
            }
        }
    }
}

/*
 * 获取并消费普通按下事件
 */
u8 Key_GetPressEvent(u8 key_index) {
    /* 防止非法数组下标 */
    if (key_index >= KEY_INPUT_COUNT)
        return 0;

    /* 当前没有按下事件 */
    if (key_press_event[key_index] == 0)
        return 0;

    /* 消费本次事件 */
    key_press_event[key_index] = 0;

    return 1;
}

/*
 * 获取并消费短按事件
 */
u8 Key_GetShortPressEvent(u8 key_index) {
    /* 防止非法数组下标 */
    if (key_index >= KEY_INPUT_COUNT)
        return 0;

    /* 当前没有短按事件 */
    if (key_short_event[key_index] == 0)
        return 0;

    /* 消费本次短按事件 */
    key_short_event[key_index] = 0;

    return 1;
}

/*
 * 获取并消费长按事件
 */
u8 Key_GetLongPressEvent(u8 key_index) {
    /* 防止非法数组下标 */
    if (key_index >= KEY_INPUT_COUNT)
        return 0;

    /* 当前没有长按事件 */
    if (key_long_event[key_index] == 0)
        return 0;

    /* 消费本次长按事件 */
    key_long_event[key_index] = 0;

    return 1;
}

/*
 * 查询按键当前是否按下
 */
u8 Key_IsPressed(u8 key_index) {
    /* 防止非法数组下标 */
    if (key_index >= KEY_INPUT_COUNT)
        return 0;

    /*
     * 这里返回消抖后的稳定状态，
     * 不直接返回原始GPIO电平
     */
    return key_stable_state[key_index];
}