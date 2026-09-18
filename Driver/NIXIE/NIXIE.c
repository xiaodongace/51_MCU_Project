#include "NIXIE.h"

#define GET_BIT_VAL(byte, pos)	(byte & (1 << pos))

//#define NOP_TIME() NOP40()	// 用于看logic分析仪
#define NOP_TIME() NOP2()

// 锁存操作 - 多行宏定义
#define RCK_ACTION() 		\
		NIXIE_RCK = 0;		\
		NOP_TIME();			\
		NIXIE_RCK = 1;		\
		NOP_TIME();


// 初始化
void Nixie_init(){
		NIXIE_PIN_INIT();
}

static void NIXIE_out(u8 dat){
		char i;
		// 8bit，先发出去的会作为高位
		for(i = 7; i >= 0; i--){            
			NIXIE_DI = GET_BIT_VAL(dat, i);
			// 寄存器的移位操作
			NIXIE_SCK = 0;
			NOP_TIME(); // 休眠一会儿
			NIXIE_SCK = 1;
			NOP_TIME(); // 休眠一会儿
		}
}

// 在某一位显示数字
void Nixie_show(num, idx){
		
		NIXIE_out(num);
	
		NIXIE_out(idx);
    
		// 锁存操作
        RCK_ACTION();
}

u8 code LED_TABLE[] = 
{
	// 0 	1	 2	-> 9	(索引012...9)
	0xC0,0xF9,0xA4,0xB0,0x99,0x92,0x82,0xF8,0x80,0x90,
	// 0. 1. 2. -> 9.	(索引10,11,12....19)
    0x40,0x79,0x24,0x30,0x19,0x12,0x02,0x78,0x00,0x10,
	// . -						(索引20,21)
	0x7F, 0xBF,
	// AbCdEFHJLPqU		(索引22,23,24....33)
	0x88,0x83,0xC6,0xA1,0x86,0x8E,0x89,0xF1,0xC7,0x8C,0x98,0xC1
};

// 显示函数
void Nixie_display(num, idx){
		u8 a_dat = LED_TABLE[num];	// 0001 0010	字母位
		u8 b_idx = 1 << idx;					// 0010 0000	数字位 5
    
		Nixie_show(a_dat, b_idx);
}

void Nixie_task(){
		u8 i;
		for(i = 0;i < 8;i++){
				Nixie_display(i+1, i);
		}
}

u8 display_buf[8] = {1,2,3,4,5,6,7,8};

static u16 Nixie_change_ms = 0;

// 扫描
void Nixie_Scan2ms(){
		
}


// 走马灯数码管
u8 code nixie_pos[][2] = {
	{0, 0}, {0, 1}, {0, 2}, 
	{1, 3}, {1, 2}, {1, 1},
	{2, 0}, {2, 1}, {2, 2}, 
	{3, 3}, {3, 2}, {3, 1},
	{4, 0}, {4, 1}, {4, 2}, 
	{5, 3}, {5, 2}, {5, 1},
	{6, 0}, {6, 1}, {6, 2},
	{7, 3}, {7, 2}, {7, 1},

	{7, 0}, {7, 5}, {7, 4},   // a, f, e
	{6, 3}, {6, 4}, {6, 5},   // d, e, f
	{5, 0}, {5, 5}, {5, 4},
	{4, 3}, {4, 4}, {4, 5},
	{3, 0}, {3, 5}, {3, 4},
	{2, 3}, {2, 4}, {2, 5},
	{1, 0}, {1, 5}, {1, 4},
	{0, 3}, {0, 4}, {0, 5}
};

// 数码管扫描: 由外部周期调用, 每 20ms 扫一个位置, 循环滚动
void Nixie_Run() {
    static u16 timer_now = 0;               // 上次扫描的系统时刻, static 跨调用保持
    static u8  scan_idx  = 0;               // 当前扫描位置下标, 扫完一轮归零
    u16 now = Timers_GetSystemMs();         // 读取系统毫秒时间

    u8 com = nixie_pos[scan_idx][0];        // COM 位选: 选择第几位数码管
    u8 seg = nixie_pos[scan_idx][1];        // SEG 段选: 选择点亮的段

    // 非阻塞定时: 距上次扫描不足 20ms 直接返回
    // now - timer_now 是 u16 无符号减法, 溢出回绕后判断依然正确
    if (now - timer_now < 100) return;
    timer_now = now;                        // 更新节拍基准

    // 段码对应位清 0 点亮, 位码对应位写 1 选通
    Nixie_show(~(1 << seg), 1 << com);

    // 下标前移, 扫完一轮回到开头
    if (++scan_idx >= sizeof(nixie_pos) / sizeof(nixie_pos[0])) {
        scan_idx = 0;
    }
}
