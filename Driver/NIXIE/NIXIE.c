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
//	// AbCdEFHJLPqU		(索引22,23,24....33)
//	0x88,0x83,0xC6,0xA1,0x86,0x8E,0x89,0xF1,0xC7,0x8C,0x98,0xC1
	// 空白 (索引22)
		0xFF
};

// 显示函数
void Nixie_display(num, idx){
		u8 a_dat = LED_TABLE[num];	// 0001 0010	字母位
		u8 b_idx = 1 << idx;					// 0010 0000	数字位 5
    
		Nixie_show(a_dat, b_idx);
}

void Nixie_task(void)
{
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


/* 日期页面：20260920 */
volatile u8 date_page[8] = {
    2, 0, 2, 16, 0, 19, 2, 10
};

void Nixie_SetDigits(u8 *digits)
{
    u8 i;
    u8 old_ea = EA;

    EA = 0;

    for(i = 0; i < 8; i++)
    {
        nixie_digits[i] = digits[i];
    }

    EA = old_ea;
}

u8 hour = 14;
u8 minute = 59;
u8 second = 50;

/* 时间页面 */
volatile u8 time_page[8];

void Update_Time_Page(void)
{
		time_page[2] = 21;
    time_page[5] = 21;
	
    time_page[0] = hour / 10;
    time_page[1] = hour % 10;

    time_page[3] = minute / 10;
    time_page[4] = minute % 10;

    time_page[6] = second / 10;
    time_page[7] = second % 10; 
}

/* 时钟递增 */
void Clock_Update(void)
{
    second++;

    if(second >= 60)
    {
        second = 0;
        minute++;

        if(minute >= 60)
        {
            minute = 0;
            hour++;

            if(hour >= 24)
            {
                hour = 0;
            }
        }
    }
}

///* 8个数码管显示的数字 */		
volatile u8 nixie_digits[8] = {0, 0, 0, 0, 0, 0, 0, 0};
volatile u8 nixie_scan_pos = 0;

void Nixie_Refresh(void)
{
    Nixie_display(nixie_digits[nixie_scan_pos],
                  nixie_scan_pos);

    nixie_scan_pos++;

    if (nixie_scan_pos >= 8) {
        nixie_scan_pos = 0;
    }
}

/*
	后续可以使用RTC时钟获取时间
	u32 date_value;
	date_value = year * 10000UL
			+ month * 100UL
			+ day;

	Nixie_SetNumber(date_value);
*/
//void Nixie_SetNumber(u32 number) {
//    u8 i;
//    u8 old_ea = EA;

//    EA = 0;
//    for (i = 0; i < 8; i++) {
//        time_page[7 - i] = (u8)(number % 10UL);
//        number /= 10UL;
//    }
//    EA = old_ea;
//}

/*
 * 关闭数码管显示。
 * 只向移位寄存器发送全灭数据，不改变GPIO模式，
 * 因此后续调用Nixie_Refresh()或Nixie_Run()可以直接恢复显示。
 */
void Nixie_Close(void) {
		/* 段码0xFF为全灭，位选0x00为不选择任何位 */
		Nixie_show(0xFF, 0x00);
}
