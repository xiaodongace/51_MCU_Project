/*
 * I2C_Lock.c - I2C 总线互斥实现
 *
 * 为什么不用 RTX51 的 os_wait 做信号量：
 *   RTX51 Tiny 不提供信号量。最小可用的实现就是一个标志位，
 *   关键是"测试 + 置位"这两步不能被中断打断，否则两个任务可能同时拿到锁。
 *   这里用"关中断 -> 读 -> 写 -> 开中断"保证原子性。
 *
 * 为什么拿不到锁时用 os_wait2 而不是空转：
 *   空转会一直占着 CPU，别的任务没法跑（锁也就永远释放不了）。
 *   os_wait2(K_TMO, 1) 让出 5ms，持锁的任务就有机会跑完并释放。
 */
#include "I2C_Lock.h"
#include "App_Public.h"

static u8 s_locked = 0;

u8 I2C_IsLocked(void)
{
    return s_locked;
}

u8 I2C_TryLock(void)
{
    u8 got;
    u8 ea;

    ea = EA;
    EA = 0;                 /* 关中断：保证"读-判-写"三步是原子的 */

    if (s_locked == 0)
    {
        s_locked = 1;
        got = 1;
    }
    else
    {
        got = 0;
    }

    EA = ea;
    return got;
}

void I2C_Lock(void)
{
    /* 超时上限设得比"最长一次持锁"大很多，正常情况下永远不会触发。
     *
     * 【真机修正】原来是 100 次 x 5ms = 500ms，而且超时后**强制夺锁**。
     * 夺锁的后果是两个任务同时操作同一个 I2C 控制器与同一条总线，
     * SSD1306 的"设置地址 -> 连续写数据"会被切碎 ——
     * 正是"按键按多了副屏就黑屏 / 只显示一半、有时只能重启"的元凶。
     * 配合 App_Display.c 把持锁改成"按页分段"（单次约 10ms），
     * 这里把上限提到 4 秒，夺锁实际上不可能发生。
     *
     * 为什么不能干脆去掉超时：规范第二节点名"不许无超时死等"。
     * 万一将来真有人写出持锁不还的代码，这里也不会把整个界面锁死。 */
    u16 wait = 0;

    while (!I2C_TryLock())
    {
        os_wait2(K_TMO, 1);     /* 让出 5ms，持锁的任务才有机会跑完 */
        wait++;

        /* 【2026-09-18 修正】原来这里在 500ms 处也 printf 一行。
         * 真机日志显示它会在若干秒内反复触发 —— 又一次把 printf 放进了热路径，
         * 而 printf 是 C51 里栈开销最大的函数（链接映射里 FREE_STACK 只剩 20 字节）。
         * 现在只在 4 秒真超时、需要强夺锁这种"本该不发生"的时刻打印一次。 */
        if (wait > 800)         /* 800 x 5ms = 4s */
        {
            printf("[I2C] lock timeout 4s, force take (不该出现，请查持锁方)\r\n");
            EA = 0;
            s_locked = 1;
            EA = 1;
            return;
        }
    }
}

void I2C_Unlock(void)
{
    u8 ea;

    ea = EA;
    EA = 0;
    s_locked = 0;
    EA = ea;
}
