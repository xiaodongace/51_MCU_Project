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
void SPI_OLED_Display_128x64(u8 *dp);
void SPI_OLED_Display_16x16(u8 x,u8 y,u8 *dp);
void SPI_OLED_Display_8x16(u8 x,u8 y,u8 *dp);
void SPI_OLED_Display_5x7(u8 x,u8 y,u8 *dp);
void Send_Command_to_ROM(u8 dat);
u8 Get_data_from_ROM(void);
void SPI_OLED_get_data_from_ROM(u8 addrHigh,u8 addrMid,u8 addrLow,u8 *pbuff,u8 DataLen);
void SPI_OLED_Display_GB2312_string(u8 x,u8 y,u8 *text);
void SPI_OLED_Display_string_5x7(u8 x,u8 y,u8 *text);
/* 本项目修正：形参由 float 改为 num100 = 数值 x 100，避免链入浮点库 */
void SPI_OLED_ShowNum(u8 x,u8 y,u32 num100,u8 len);
void SPI_OLED_Init(void);


//=====================================开机动画
void SPI_OLED_Refresh();                                              // 刷新显示
void SPI_OLED_RefreshPart(u8 xstart, u8 ystart, u8 width, u8 height); // 刷新指定区域
void SPI_OLED_GFill();                                                // 全屏填充
void SPI_OLED_GClear();                                               // 全屏清除
void SPI_OLED_DrawPoint(u8 x, u8 y);                                  // 绘制点
void SPI_OLED_ClearPoint(u8 x, u8 y);                                 // 清除点


#endif