#ifndef __SPI_OLED_H
#define __SPI_OLED_H

#include "config.h"
#include "GPIO.h"
#include "MATH.H"

// SCLK
sbit SPI_OLED_SCL=P5^0;    
// MOSI
sbit SPI_OLED_SDA=P1^3;
// 命令0  数据1 切换
sbit SPI_OLED_DC =P1^6; 
// 屏幕片选线 (拉低有效)
sbit SPI_OLED_CS =P4^7;
// MISO
sbit SPI_OLED_READ_FS0=P1^1;
// 字库片选线 (拉低有效)
sbit SPI_OLED_ROM_CS=P1^0;

//#define u8  unsigned char
//#define u32 unsigned long
#define SPI_OLED_CMD  0   //写命令
#define SPI_OLED_DATA 1   //写数据

#define SPI_OLED_SCL_Set()       SPI_OLED_SCL=1;
#define SPI_OLED_SCL_Clr()       SPI_OLED_SCL=0;

#define SPI_OLED_SDA_Set()       SPI_OLED_SDA=1;
#define SPI_OLED_SDA_Clr()       SPI_OLED_SDA=0;

#define SPI_OLED_DC_Set()        SPI_OLED_DC=1;
#define SPI_OLED_DC_Clr()        SPI_OLED_DC=0;

#define SPI_OLED_CS_Set()        SPI_OLED_CS=1;
#define SPI_OLED_CS_Clr()        SPI_OLED_CS=0;

#define SPI_OLED_ROM_CS_Set()    SPI_OLED_ROM_CS=1;
#define SPI_OLED_ROM_CS_Clr()    SPI_OLED_ROM_CS=0;

//void delay_ms(unsigned int ms);
/* 开关显示（.c 里有实现，原厂头文件漏了声明，这里补上） */
void SPI_OLED_DisPlay_On(void);
void SPI_OLED_DisPlay_Off(void);

void SPI_OLED_ColorTurn(u8 i);
void SPI_OLED_DisplayTurn(u8 i);
void SPI_OLED_WR_Byte(u8 dat,u8 cmd);
void SPI_OLED_Clear(void);
void SPI_OLED_address(u8 x,u8 y);
void SPI_OLED_Display_16x16(u8 x,u8 y,u8 *dp);
void SPI_OLED_Display_8x16(u8 x,u8 y,u8 *dp);
void Send_Command_to_ROM(u8 dat);
u8 Get_data_from_ROM(void);
void SPI_OLED_get_data_from_ROM(u8 addrHigh,u8 addrMid,u8 addrLow,u8 *pbuff,u8 DataLen);
void SPI_OLED_Display_GB2312_string(u8 x,u8 y,u8 *text);
void SPI_OLED_Init(void);


/*========================================================================
 *  ★★【必须记住】本驱动有**两条互不相容**的绘制通路，绝不能混用
 *
 *  A. 直写屏通路（菜单/所有页面用）
 *       SPI_OLED_Clear()                   清屏（逐字节写 0 到屏）
 *       SPI_OLED_Display_GB2312_string()  写字（逐字节写点阵到屏）
 *     · 数据直接进 OLED，**完全不经过显存**
 *     · 不配套使用会怎样：只 Clear 不写字 = 白屏/黑屏（取决于反色）
 *
 *  B. 显存通路（游戏用）
 *       SPI_OLED_GClear()     把显存[1024]清零
 *       SPI_OLED_DrawPoint()  往显存写一个点
 *       SPI_OLED_GBuf_8x16() / SPI_OLED_GBuf_string()  往显存写 ASCII
 *       SPI_OLED_Refresh()    把显存[1024]整屏送上屏
 *     · 数据先进显存，最后一次 Refresh 才上屏
 *
 *  ★ 混用的后果（2026-09-23 真机踩过）：
 *       GClear()                        // 清的是**显存**
 *       Display_GB2312_string(...)      // 直写屏 —— 显存里其实什么都没有
 *       Refresh()                       // 把**空显存**覆盖上去
 *     -> 刚写好的字被自己擦掉，画面全黑。
 *     当时的表现是"游戏结束不显示分数、黑屏 3 秒才回主界面"。
 *
 *  ★ 判据：**同一段代码里，Clear 和 写字 必须同属一条通路**。
 *    菜单那种页面级界面走 A；游戏/动画那种"整屏重绘 + 落点画图"走 B。
 *========================================================================*/

//=====================================显存通路（游戏/开机动画）
void SPI_OLED_Refresh();                                              // 刷新显示
void SPI_OLED_GClear();                                               // 全屏清除
void SPI_OLED_DrawPoint(u8 x, u8 y);                                  // 绘制点
void SPI_OLED_ClearPoint(u8 x, u8 y);                                 // 清除点
/* 往**显存**写一个 8x16 的 ASCII 点阵（16 字节：前 8 上半页、后 8 下半页）*/
void SPI_OLED_GBuf_8x16(u8 x, u8 page, u8 *dp);
/* 在**显存**上画一串 ASCII（点阵从屏上字库 IC 读；只支持 0x20..0x7E）*/
void SPI_OLED_GBuf_string(u8 x, u8 page, u8 *text);


#endif