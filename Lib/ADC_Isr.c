/*
 * ADC_Isr.c - 【2026-09-22 已废弃，文件保留仅作说明，不含任何函数】
 *
 * 原来里面是 STC 官方模板的 ADC_ISR_Handler 空壳（只清一下中断标志）。
 * 本工程的 ADC 是**查询式**用的（Driver/NTC.c 里 Get_ADCResult() 等结果），
 * 中断从来没使能过：App_System.c 里是 NVIC_ADC_Init(DISABLE, Priority_0)。
 * 所以这个中断服务永远不会被执行，删掉它连中断向量一起省。
 *
 * 为什么不直接从工程移除：Keil 的 UV4 命令行编译会把 <File> 列表写回 .uvproj，
 * 手工删掉的节点会被恢复（详见 Lib/Soft_I2C.c 里同样的说明）。
 * 以后要改成中断式采样：从 STC 官方库把 ADC_ISR_Handler 拷回本文件即可。
 */
