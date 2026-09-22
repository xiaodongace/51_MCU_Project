/*
 * I2C_Isr.c - 【2026-09-22 已废弃，文件保留仅作说明，不含任何函数】
 *
 * 原来里面是 STC 官方库的**从机模式**中断处理（I2C_ISR_Handler + I2CIsr 结构体
 * + DisplayFlag 标志，共 179 字节），配套还有 Lib/I2C.c 里的 I2C_Buffer[8]。
 *
 * 本工程用不到它：
 *   1) I2C 中断从来没使能 —— App_System.c 和 Driver/I2C_OLED/I2C_OLED.c 里都是
 *      NVIC_I2C_Init(I2C_Mode_Master, DISABLE, Priority_0)，第二个参数是 DISABLE；
 *   2) 那两个"从机模式收包"的函数（I2C_ReadNbyte / I2C_WriteNbyte）本身是**查询式**的，
 *      靠轮询 I2CMSST 的状态位完成收发，不依赖中断。
 * 所以这个中断服务永远不会被执行，删掉它连中断向量一起省。
 *
 * 为什么不直接从工程移除：见 Lib/Soft_I2C.c 的说明（Keil 会回写 uvproj）。
 * 以后要接从机模式：从 STC 官方库把 I2C_ISR_Handler 与 I2CIsr 拷回即可，
 * 并记得在 Lib/I2C.c 里把 I2C_Buffer[8] 一起恢复。
 */
