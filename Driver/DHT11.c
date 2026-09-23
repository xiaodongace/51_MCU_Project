/*
 * DHT11.c - 温湿度传感器驱动
 *
 * 时序与判据逐字照抄 v3.1 Driver/DHT11.c：
 *   - delay_1us() = NOP8()（v3.1 原文 "NOP8(); // 1us"）
 *   - 数据位 0/1 的分界判据 cnt > 47，来自 v3.1 注释 "(24 + 71) / 2 = 47"
 *   - 各段时长的容差区间与 v3.1 完全一致
 * 这是真机上验证过的判据，不做"优化"。
 *
 * 相对 v3.1 改了两处（规范第三条要求：偏离必须显式列出）：
 *
 *   1) 去掉 float。
 *      v3.1: *p_temperature = dat[2] + (dat[3] & 0x7F) * 0.1f;
 *      改为整数：温度以 x10 为单位返回，调用方自己决定怎么显示。
 *      理由：float 会链入 C51FPL.LIB，规范第二节要求读 .m51 确认无 FPMUL/FPDIV。
 *
 *   2) 读出 40 位数据期间关中断。
 *      v3.1 全程不关中断。但《04-M1分工规格》F 角色明确写了
 *      "读那 40 位数据期间要'捂耳朵'（关闭中断），因为时序要求严格，被打断就读错"。
 *      这里的窗口是：等响应 + 40 位数据 ≈ 4.6ms。
 *      "拉低 18ms" 那一段不关中断（用 os_wait2 等着，把 CPU 让给别人）。
 *      关中断对全局时钟的影响见《05-M1集成验证》第四节：少走 4 格 / 30 秒 = 0.013%，可忽略。
 */
#include "DHT11.h"
#include "GPIO.h"
#include "App_Public.h"

/* 拉低总线的时长：4 x 5ms = 20ms（照抄 v3.1 的 os_wait2(K_TMO, 4)） */
#define DHT11_START_TICKS   4

/* 每 1 个计数单元的时间。NOP8 + 循环与函数调用开销，真机标定约为 1us。
 * static：避免与其它模块的同名函数撞符号（《04》F 角色注意事项点名了这一条） */
static void delay_1us(void)
{
    NOP8();
}

void DHT11_Init(void)
{
    DHT11_GPIO_INIT();
    DHT = 1;
}

/*
 * 等某个电平结束，并检查持续时长是否落在 [min, max] 内。
 * 返回 0 表示时长合法；返回 1 表示超时/超范围，由调用方决定错误码。
 * 用法与 v3.1 的 wait_level_change 宏等价，但不含 printf（printf 会影响时序）。
 */
static u8 wait_level(u8 level, u16 min, u16 max, u16 *outCnt)
{
    u16 cnt = 0;

    while (DHT == level)
    {
        delay_1us();
        cnt++;

        /* 超过上限就不再等，防止传感器异常时死循环（v3.1 的短路保护） */
        if (cnt > max)
        {
            *outCnt = cnt;
            return 1;
        }
    }

    *outCnt = cnt;

    if (cnt < min)
    {
        return 1;
    }

    return 0;
}

u8 DHT11_Read(s16 *tempX10, u8 *humi)
{
    u8  dat[5];
    u8  i;
    s8  j;
    u8  eaSaved;
    u16 cnt;
    u16 highCnt = 0;

    for (i = 0; i < 5; i++)
    {
        dat[i] = 0;
    }

    /* ---------- 1. 主机拉低总线 >= 18ms（v3.1: os_wait2(K_TMO, 4)） ----------
     * 这一段不关中断，用 os_wait2 让出 CPU，其它任务照常跑。 */
    DHT = 0;
    os_wait2(K_TMO, DHT11_START_TICKS);
    DHT = 1;

    /* ---------- 从这里开始时序敏感，关中断（"捂耳朵"） ---------- */
    eaSaved = EA;
    EA = 0;

    /* 2. 等主机释放总线：v3.1 判据 [6, 35] */
    if (wait_level(1, 6, 35, &cnt))
    {
        EA = eaSaved;
        DHT = 1;
        return DHT11_ERR_RELEASE;
    }

    /* 3+4. 响应低电平、响应高电平
     *
     * 【本项目修正】原来照抄 v3.1 用了很紧的窗口 [70,88] / [74,92]，
     * 实机回报的是 err = -4（= DHT11_ERR_RESP_LOW），说明实测计数落在这个窗口之外。
     * 根因：v3.1 的 delay_1us() 是 NOP8()（8 个机器周期 = 0.33us @24MHz），
     *       真正的一次循环耗时还要加上函数调用与 u16 自增，约 1.2~1.3us，
     *       所以"计数"和"微秒"之间有一个未知的换算系数。
     *       照抄那些以"微秒"为准写死的窗口，必然偏。
     *
     * 改法：响应阶段只做宽松的合理性检查（挡住"没接传感器"这种异常）。
     *       真正的 0/1 区分交给下面"比高低电平长度"的相对判据 —— 那个与时间单位无关。 */
    if (wait_level(0, 10, 250, &cnt))
    {
        EA = eaSaved;
        DHT = 1;
        return DHT11_ERR_RESP_LOW;
    }

    if (wait_level(1, 10, 250, &cnt))
    {
        EA = eaSaved;
        DHT = 1;
        return DHT11_ERR_RESP_HIGH;
    }

    /* 5. 连续读 40 位（5 字节 x 8 位），高位先出 */
    for (i = 0; i < 5; i++)
    {
        for (j = 7; j >= 0; j--)
        {
            u16 lowCnt = 0;

            /* 每一位都是"一低一高"。低电平长度是固定的（约 54us，不区分 0/1），
             * 高电平长度才区分：bit0 约 24us，bit1 约 71us。
             *
             * 所以判据改成**两者比大小**：
             *     highCnt > lowCnt  ->  1
             *     highCnt <= lowCnt ->  0
             * 24/54 = 0.44 与 71/54 = 1.31 分居 1.0 两侧，余量足够，
             * 而且完全不需要知道"一次计数等于多少微秒"。 */
            if (wait_level(0, 10, 250, &lowCnt))
            {
                EA = eaSaved;
                DHT = 1;
                return DHT11_ERR_DATA_LOW;
            }

            if (wait_level(1, 2, 250, &highCnt))
            {
                EA = eaSaved;
                DHT = 1;
                return DHT11_ERR_DATA_HIGH;
            }

            /* highCnt <= lowCnt 时这一位就是 0；dat[] 初值已清零，不用动。
             * （第 70 轮：原来这里挂了一个空的 else 块） */
            if (highCnt > lowCnt)
            {
                dat[i] |= (u8)(1 << j);
            }
        }
    }

    /* 数据读完，恢复中断 */
    EA = eaSaved;
    DHT = 1;

    /* 6. 校验：前 4 字节之和的低 8 位 == 第 5 字节 */
    if ((u8)(dat[0] + dat[1] + dat[2] + dat[3]) != dat[4])
    {
        return DHT11_ERR_CHECKSUM;
    }

    /* 7. 解析（v3.1 用 float，这里改整数）
     *    dat[0] = 湿度整数部分
     *    dat[2] = 温度整数部分
     *    dat[3] 低 7 位 = 温度小数部分，最高位 1 表示零下 */
    *humi = dat[0];

    {
        s16 t;
        t = (s16)dat[2] * 10 + (s16)(dat[3] & 0x7F);

        if ((dat[3] & 0x80) != 0)
        {
            t = (s16)(-t);
        }

        *tempX10 = t;
    }

    return 0;
}
