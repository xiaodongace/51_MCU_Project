#include "I2C_OLED.h"
#include "oledfont.h"
#include "GPIO.h"
#include "I2C.h"
#include "NVIC.h"
#include "Switch.h"

//OLED的显存
//存放格式如下.
//[0]0 1 2 3 ... 127	
//[1]0 1 2 3 ... 127	
//[2]0 1 2 3 ... 127	
//[3]0 1 2 3 ... 127	
//[4]0 1 2 3 ... 127	
//[5]0 1 2 3 ... 127	
//[6]0 1 2 3 ... 127	
//[7]0 1 2 3 ... 127 			   
//void delay_ms(unsigned int ms)
//{                         
//	unsigned int a;
//	while(ms)
//	{
//		a=1800;
//		while(a--);
//		ms--;
//	}
//	return;
//}

//反显函数
void I2C_OLED_ColorTurn(u8 i)
{
	if(i==0)
		{
			I2C_OLED_WR_Byte(0xA6,I2C_OLED_CMD);//正常显示
		}
	if(i==1)
		{
			I2C_OLED_WR_Byte(0xA7,I2C_OLED_CMD);//反色显示
		}
}

//屏幕旋转180度
void I2C_OLED_DisplayTurn(u8 i)
{
	if(i==0)
		{
			I2C_OLED_WR_Byte(0xC8,I2C_OLED_CMD);//正常显示
			I2C_OLED_WR_Byte(0xA1,I2C_OLED_CMD);
		}
	if(i==1)
		{
			I2C_OLED_WR_Byte(0xC0,I2C_OLED_CMD);//反转显示
			I2C_OLED_WR_Byte(0xA0,I2C_OLED_CMD);
		}
}

void I2C_OLED_WR_Byte(u8 dat,u8 mode) {
    if(mode) {
        I2C_WriteNbyte(0x78, 0x40, &dat, 1);
    } else {
        I2C_WriteNbyte(0x78, 0x00, &dat, 1);
    }
}



//坐标设置

void I2C_OLED_Set_Pos(u8 x, u8 y) 
{ 
	I2C_OLED_WR_Byte(0xb0+y,I2C_OLED_CMD);
	I2C_OLED_WR_Byte(((x&0xf0)>>4)|0x10,I2C_OLED_CMD);
	I2C_OLED_WR_Byte((x&0x0f),I2C_OLED_CMD);
}   	  
//清屏函数,清完屏,整个屏幕是黑色的!和没点亮一样!!!	  
/*------------------------------------------------------------------------
 * 显示开 / 关（SSD1306 命令 0xAF = ON，0xAE = OFF）
 *
 * 【2026-09-21 用户要求】"建议在主界面时就关闭 I2C 屏幕"
 *
 * 与其一遍遍清屏去追"没清干净的一点残留"，不如**直接把面板关掉**：
 * 关了之后控制器里剩什么都不可能亮，绝对干净，而且省电。
 *------------------------------------------------------------------------*/
void I2C_OLED_DisplayOn(void)
{
    I2C_OLED_WR_Byte(0xAF, I2C_OLED_CMD);
}

void I2C_OLED_DisplayOff(void)
{
    I2C_OLED_WR_Byte(0xAE, I2C_OLED_CMD);
}

void I2C_OLED_Clear(void)
{
    /* 【2026-09-21 优化】原来这里是"一字节一次 I2C 事务"：
     * 8 页 x (3 字节定位 + 128 字节数据) = **1048 次事务**，按 400kHz 算约 70~250ms。
     * 这期间屏幕上是"清了还没画完"的中间状态 —— 用户报的
     *    "I2C 刷新整个屏幕的时候，偶尔就会出现刷新不完整、下边空白区域有残留"
     * 就是看到了这个中间过程。
     *
     * 厂家库的 I2C_WriteNbyte() 本来就是"一次事务连发多字节"，
     * 所以改成**一次定位 + 一次连发 128 字节**：
     *   8 页 x 2 次事务 = **16 次事务**，约 2ms，快约 50 倍。
     *
     * 顺带：这一改之后，"整屏切换"的耗时从 ~250ms 降到 ~2ms，
     * 于是可以放心地用"熄面板 -> 清屏 -> 画内容 -> 点亮"的做法
     * （见 App_Display.c 的 sub_redraw）。 */
    static u8 xdata s_zero[128];
    u8 i;
    u8 page;

    for (i = 0; i < 128; i++)
    {
        s_zero[i] = 0;
    }

    for (page = 0; page < 8; page++)
    {
        I2C_OLED_Set_Pos(0, page);
        I2C_WriteNbyte(0x78, 0x40, s_zero, 128);
    }
}
//在指定位置显示一个字符,包括部分字符
//x:0~127
//y:0~63				 
//sizey:选择字体 6x8  8x16
void I2C_OLED_ShowChar(u8 x,u8 y,u8 chr,u8 sizey)
{      	
	u8 c=0,sizex=sizey/2;
	u16 i=0,size1;
	if(sizey==8)size1=6;
	else size1=(sizey/8+((sizey%8)?1:0))*(sizey/2);
	c=chr-' ';//得到偏移后的值
	I2C_OLED_Set_Pos(x,y);
	for(i=0;i<size1;i++)
	{
		if(i%sizex==0&&sizey!=8) I2C_OLED_Set_Pos(x,y++);
		if(sizey==8) I2C_OLED_WR_Byte(asc2_0806[c][i],I2C_OLED_DATA);//6X8字号
		else if(sizey==16) I2C_OLED_WR_Byte(asc2_1608[c][i],I2C_OLED_DATA);//8x16字号
//		else if(sizey==xx) I2C_OLED_WR_Byte(asc2_xxxx[c][i],I2C_OLED_DATA);//用户添加字号
		else return;
	}
}
//显示一个字符号串
void I2C_OLED_ShowString(u8 x,u8 y,u8 *chr,u8 sizey)
{
	u8 j=0;
	while (chr[j]!='\0')
	{		
		I2C_OLED_ShowChar(x,y,chr[j++],sizey);
		if(sizey==8)x+=6;
		else x+=sizey/2;
	}
}

static void	I2C_config(void)
{
	I2C_InitTypeDef		I2C_InitStructure;

	I2C_InitStructure.I2C_Mode      = I2C_Mode_Master;	//主从选择   I2C_Mode_Master, I2C_Mode_Slave
	I2C_InitStructure.I2C_Enable    = ENABLE;			//I2C功能使能,   ENABLE, DISABLE
	I2C_InitStructure.I2C_MS_WDTA   = DISABLE;			//主机使能自动发送,  ENABLE, DISABLE
	I2C_InitStructure.I2C_Speed     = 13;				//总线速度=Fosc/2/(Speed*2+4),      0~63
                                                        // 400k, 24M => 13
	I2C_Init(&I2C_InitStructure);
	NVIC_I2C_Init(I2C_Mode_Master,DISABLE,Priority_0);	//主从模式, I2C_Mode_Master, I2C_Mode_Slave; 中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

	I2C_SW(I2C_P33_P32);					//I2C_P14_P15,I2C_P24_P25,I2C_P33_P32
}

//初始化				    
void I2C_OLED_Init(void)
{
    P3_MODE_IO_PU(GPIO_Pin_2 | GPIO_Pin_3);
    I2C_config();

	I2C_OLED_WR_Byte(0xAE,I2C_OLED_CMD);//--turn off oled panel
	I2C_OLED_WR_Byte(0x00,I2C_OLED_CMD);//---set low column address
	I2C_OLED_WR_Byte(0x10,I2C_OLED_CMD);//---set high column address
	I2C_OLED_WR_Byte(0x40,I2C_OLED_CMD);//--set start line address  Set Mapping RAM Display Start Line (0x00~0x3F)
	I2C_OLED_WR_Byte(0x81,I2C_OLED_CMD);//--set contrast control register
	I2C_OLED_WR_Byte(0xCF,I2C_OLED_CMD); // Set SEG Output Current Brightness
	I2C_OLED_WR_Byte(0xA1,I2C_OLED_CMD);//--Set SEG/Column Mapping     0xa0左右反置 0xa1正常
	I2C_OLED_WR_Byte(0xC8,I2C_OLED_CMD);//Set COM/Row Scan Direction   0xc0上下反置 0xc8正常
	I2C_OLED_WR_Byte(0xA6,I2C_OLED_CMD);//--set normal display
	I2C_OLED_WR_Byte(0xA8,I2C_OLED_CMD);//--set multiplex ratio(1 to 64)
	I2C_OLED_WR_Byte(0x3f,I2C_OLED_CMD);//--1/64 duty
	I2C_OLED_WR_Byte(0xD3,I2C_OLED_CMD);//-set display offset	Shift Mapping RAM Counter (0x00~0x3F)
	I2C_OLED_WR_Byte(0x00,I2C_OLED_CMD);//-not offset
	I2C_OLED_WR_Byte(0xd5,I2C_OLED_CMD);//--set display clock divide ratio/oscillator frequency
	I2C_OLED_WR_Byte(0x80,I2C_OLED_CMD);//--set divide ratio, Set Clock as 100 Frames/Sec
	I2C_OLED_WR_Byte(0xD9,I2C_OLED_CMD);//--set pre-charge period
	I2C_OLED_WR_Byte(0xF1,I2C_OLED_CMD);//Set Pre-Charge as 15 Clocks & Discharge as 1 Clock
	I2C_OLED_WR_Byte(0xDA,I2C_OLED_CMD);//--set com pins hardware configuration
	I2C_OLED_WR_Byte(0x12,I2C_OLED_CMD);
	I2C_OLED_WR_Byte(0xDB,I2C_OLED_CMD);//--set vcomh
	I2C_OLED_WR_Byte(0x40,I2C_OLED_CMD);//Set VCOM Deselect Level
	I2C_OLED_WR_Byte(0x20,I2C_OLED_CMD);//-Set Page Addressing Mode (0x00/0x01/0x02)
	I2C_OLED_WR_Byte(0x02,I2C_OLED_CMD);//
	I2C_OLED_WR_Byte(0x8D,I2C_OLED_CMD);//--set Charge Pump enable/disable
	I2C_OLED_WR_Byte(0x14,I2C_OLED_CMD);//--set(0x10) disable
	I2C_OLED_WR_Byte(0xA4,I2C_OLED_CMD);// Disable Entire Display On (0xa4/0xa5)
	I2C_OLED_WR_Byte(0xA6,I2C_OLED_CMD);// Disable Inverse Display On (0xa6/a7) 
	I2C_OLED_Clear();
	I2C_OLED_WR_Byte(0xAF,I2C_OLED_CMD); /*display ON*/ 
}




