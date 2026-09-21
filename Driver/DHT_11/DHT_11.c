#include "DHT_11.h"

/*
 * 等待DHT11总线从指定电平发生变化。
 * 超过最大时间仍未变化时跳转到统一清理位置，确保中断一定会恢复。
 */
#define DHT11_WAIT_LEVEL_CHANGE(level, min_us, max_us)          \
    do {                                                        \
        cnt = 0;                                                \
        while (DHT == (level) && cnt <= (max_us)) {            \
            DHT11_DELAY_1US();                                  \
            cnt++;                                              \
        }                                                       \
        if (cnt < (min_us) || cnt > (max_us)) {                \
            result = -2;                                        \
            goto read_finish;                                   \
        }                                                       \
    } while (0)

/*
 * 读取DHT11原始的5字节数据。
 * 精确接收40位数据期间临时关闭总中断，避免RTX节拍打断单总线时序。
 */
static int8 DHT11_ReadRaw(u8 dat[5]) {
    u16 data cnt;
    int8 i;
    int8 j;
    int8 result;
    u8 old_ea;

    /* 默认读取成功，出现时序或校验错误时再修改结果 */
    result = SUCCESS;
    old_ea = EA;

    /* 主机拉低总线至少18ms，通知DHT11开始发送数据 */
    DHT = 0;
    os_wait2(K_TMO, 4);

    /* 释放总线并立即进入精确时序接收阶段 */
    DHT = 1;
    EA = 0;

    /* 等待传感器把总线从高电平拉低作为响应 */
    cnt = 0;
    while (DHT == 1 && cnt < 45) {
        DHT11_DELAY_1US();
        cnt++;
    }

    /* 没有在合理时间内收到响应，认为传感器未连接或通信失败 */
    if (cnt < 6 || cnt > 35) {
        result = -1;
        goto read_finish;
    }

    /* 检查DHT11约80us的响应低电平 */
    DHT11_WAIT_LEVEL_CHANGE(0, 65, 100);

    /* 检查DHT11约80us的响应高电平 */
    DHT11_WAIT_LEVEL_CHANGE(1, 65, 105);

    /* 接收5字节、共40位数据，高位在前 */
    for (i = 0; i < 5; i++) {
        for (j = 7; j >= 0; j--) {
            /* 每一位先等待约50us低电平结束 */
            DHT11_WAIT_LEVEL_CHANGE(0, 35, 70);

            /* 再测量高电平宽度，以约47us为0和1的分界 */
            DHT11_WAIT_LEVEL_CHANGE(1, 15, 90);

            /* 高电平较长表示本位数据为1 */
            if (cnt > 47)
                dat[i] |= (1 << j);
        }
    }

    /* 校验位应等于前4字节之和的低8位 */
    if (((dat[0] + dat[1] + dat[2] + dat[3]) & 0xFF) !=
        dat[4]) {
        result = -3;
    }

read_finish:
    /* 无论成功还是失败，都先释放DHT11总线 */
    DHT = 1;

    /* 恢复进入读取函数前的总中断状态 */
    EA = old_ea;

    return result;
}

/*
 * 初始化DHT11单总线。
 * 这里只配置P4.6，不能修改P5.3等按键或I2C OLED引脚。
 */
void DHT11_Init(void) {
    /* P4.6配置为带上拉的准双向口 */
    P4_MODE_IO_PU(GPIO_Pin_6);

    /* 空闲状态释放总线 */
    DHT = 1;
}

/*
 * 读取并换算DHT11湿度和温度。
 */
int8 DHT11_get_info(
    float *p_humidity,
    float *p_temperature) {
    u8 dat[5];
    int8 result;
    float humidity;
    float temperature;

    /* 每次读取前把接收缓冲区清零 */
    dat[0] = 0;
    dat[1] = 0;
    dat[2] = 0;
    dat[3] = 0;
    dat[4] = 0;

    /* 获取DHT11原始数据 */
    result = DHT11_ReadRaw(dat);
    if (result != SUCCESS)
        return result;

    /* 湿度由整数部分和一位小数组成 */
    humidity = dat[0] + dat[1] * 0.1f;

    /* 温度由整数部分和一位小数组成 */
    temperature = dat[2] + (dat[3] & 0x7F) * 0.1f;

    /* 小数字节最高位为1时表示负温度 */
    if (dat[3] & 0x80)
        temperature = -temperature;

    /* 将换算结果写回调用者 */
    *p_humidity = humidity;
    *p_temperature = temperature;

    return SUCCESS;
}
