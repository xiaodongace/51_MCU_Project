/*
 * main.c - 程序的起点
 *
 * RTX51 Tiny 的约定：
 *   任务 0 是"初始化任务"，系统起来后先跑它；
 *   它把外设配好、把其它任务创建出来，然后销毁自己。
 *   函数名不要用 start 这类词（会和 I2C.h 里的 Start 撞名，v3.1 的注释点名过）。
 *
 * 五个任务的节奏（详见《02-技术方案》6.1 和各自文件头部的说明）：
 *   T1 渲染  5ms   刷两块屏
 *   T2 输入  10ms  按键 / 矩阵键盘 / 旋钮 -> 事件队列
 *   T3 逻辑  10ms  串口协议、闹钟调度、页面栈；内部用 g_sysTick 对准"每秒"
 *   T4 音效  5ms   换音符；每 100ms 调一次日出唤醒的亮度
 *   T5 采集  1s    读 DHT11 / NTC，每 10 分钟存一条记录
 *
 * 数码管不在这五个任务里：它由 Timer2 的 1kHz 中断刷（Driver/NixieScan.c），
 * 因为 RTX51 的心跳是 5ms，任务最快也只能 5ms 醒一次，
 * 而文档要求的"每 2ms 刷一位数码管"在 RTX51 下做不到（原因见 NixieScan.h）。
 */
#include "App_Public.h"
#include "App_System.h"
#include "delay.h"

void main_start(void) _task_ TASK_MAIN
{
    delay_ms(250);
    delay_ms(250);
    delay_ms(250);
    delay_ms(250);
    delay_ms(250);
    delay_ms(250);
    delay_ms(250);
    delay_ms(250);
    
    /* 初始化硬件：安全电平 -> EAXSFR -> 各外设 -> EA=1 -> 装载 EEPROM 与时钟 */
    sys_init();

    /* 创建五个任务（编号见 App_Public.h） */
    os_create_task(TASK_RENDER);
    os_create_task(TASK_INPUT);
    os_create_task(TASK_LOGIC);
    os_create_task(TASK_MUSIC);
    os_create_task(TASK_SENSOR);

    /* 初始化任务的工作干完了，销毁自己释放资源 */
    os_delete_task(TASK_MAIN);
}
