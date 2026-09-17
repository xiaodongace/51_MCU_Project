#include "App_Public.h"
#include "Buzzer.h"
#include "UART.h"
#include "NVIC.h"
#include "Switch.h"
#include "STC8H_PWM.h"

#define BUZZER	P00



//			              C	   D    E 	 F	  G	   A	B	 C`
static u16 code hz[] = {523, 587, 659, 698, 784, 880, 988, 1047};
//	葫芦娃
static u16 code HLW[] = {
    // 葫芦娃，葫芦娃       一根藤上七朵花                    风吹雨打都不怕
    1, 1, 3,   1, 1, 3,     6, 6, 6, 5, 6, 5, 1, 3, 3,    1, 6, 6, 5, 6,   5, 1, 2, 2
};
// 葫芦娃延时
static u16 code HLW_dur[] = {
    // 葫芦娃，葫芦娃        一根藤上七朵花                   风吹雨打都不怕
    4, 4, 4,   4, 4, 4,     2, 2, 4, 4, 4, 4, 4, 4, 4,    2, 2, 4, 4, 4,   4, 4, 4, 4,
};

// 白龙马
static u16 code BLM[] = {
    // 白龙马，蹄朝西        驮着唐三藏跟着仨徒弟                  西天取经上大路               一走就是几万里
    3, 3, 6, 7, 6, 5, 6,    6, 1, 1, 1, 1, 1, 6, 1, 2, 3, 3,    3, 5, 6, 1, 6, 6, 5, 3,     2, 2, 2, 2, 3, 5, 7, 6
};
// 白龙马延时
static u16 code BLM_dur[] = {
    // 白龙马，蹄朝西        驮着唐三藏跟着仨徒弟                  西天取经上大路               一走就是几万里
    4, 4, 4, 4, 4, 4, 8,    3, 1, 1, 1, 1, 1, 3, 1, 3, 3, 6,    3, 3, 3, 3, 3, 3, 3, 6,     1, 1, 1, 1, 3, 3, 3, 6
};

// 两只老虎
static u16 code LZLH[] = {
    1, 2, 3, 1,        1, 2, 3, 1,          3, 4, 5,  3, 4, 5, 
    5, 6, 5, 4, 3, 1,  5, 6, 5, 4, 3, 1,    1, 5, 1,  1, 5, 1,
};
// 两只老虎延时
static u16 code LZLH_dur[] = {
    4, 4, 4, 4,        4, 4, 4, 4,          4, 4, 8,  4, 4, 8, 
    3, 1, 3, 1, 4, 4,  3, 1, 3, 1, 4, 4,    4, 4, 8,  4, 4, 8, 
};

// 三首音乐（指针数组，指向 code 区的各旋律）
static u16 code * code music[] = { HLW, BLM, LZLH };
// 音乐延时
static u16 code * code music_dur[] = { HLW_dur, BLM_dur, LZLH_dur };
// 每首音乐的长度（由编译器算，与各旋律数组同步）
static u16 code music_len[] = {
    sizeof(HLW) / sizeof(HLW[0]),
    sizeof(BLM) / sizeof(BLM[0]),
    sizeof(LZLH) / sizeof(LZLH[0]),
};

// 全局播放状态(供 App_Buzzer.c 通过按键修改)
char Volume = 1;
char Song = 0;

// 非阻塞播放状态机
#define BUZZER_STOP    0
#define BUZZER_PLAY    1
#define BUZZER_PAUSE   2
#define BUZZER_PAUSING 3   // 待暂停:当前音符播完后再暂停

static u8  buzzer_state = BUZZER_STOP;  // 当前状态
static u16 note_index   = 0;            // 当前音符下标
static u16 note_remain  = 0;            // 当前音符剩余拍数(单位:任务循环轮数)


// 初始化蜂鸣器引脚
void Buzzer_GPIO(void) {
    // 初始化串口引脚
    P3_MODE_IO_PU(GPIO_Pin_0 | GPIO_Pin_1);
    // 初始化蜂鸣器引脚(P00, PWM5 输出)
    P0_MODE_OUT_PP(GPIO_Pin_0);
}

// 初始化串口
void Buzzer_Uart(void) {
    // >>> 记得添加 NVIC.c, UART.c, UART_Isr.c <<<
    COMx_InitDefine		COMx_InitStructure;					//结构定义
	// ================UART1  P30 P31================
    COMx_InitStructure.UART_Mode      = UART_8bit_BRTx;	//模式, UART_ShiftRight,UART_8bit_BRTx,UART_9bit,UART_9bit_BRTx
    COMx_InitStructure.UART_BRT_Use   = BRT_Timer1;			//选择波特率发生器, BRT_Timer1, BRT_Timer2 (注意: 串口2固定使用BRT_Timer2)
    COMx_InitStructure.UART_BaudRate  = 115200ul;			//波特率, 一般 110 ~ 115200
    COMx_InitStructure.UART_RxEnable  = ENABLE;				//接收允许,   ENABLE或DISABLE
    COMx_InitStructure.BaudRateDouble = DISABLE;			//波特率加倍, ENABLE或DISABLE
    UART_Configuration(UART1, &COMx_InitStructure);		//初始化串口1 UART1,UART2,UART3,UART4

    NVIC_UART1_Init(ENABLE,Priority_1);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3
    UART1_SW(UART1_SW_P30_P31);		// 引脚选择, UART1_SW_P30_P31,UART1_SW_P36_P37,UART1_SW_P16_P17,UART1_SW_P43_P44
}

void Buzzer_PWM(u16 hz_value)
{
	PWMx_InitDefine		PWMx_InitStructure;
    
    u16 period = (MAIN_Fosc / hz_value);
	
	// 配置PWM5
	PWMx_InitStructure.PWM_Mode    		= CCMRn_PWM_MODE1;	//模式,		CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2
	PWMx_InitStructure.PWM_Duty   	 	= (u32)period * 3 / 10;	//PWM占空比时间, 0~Period
	PWMx_InitStructure.PWM_EnoSelect    = ENO5P;			//输出通道选择,	ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P
	PWM_Configuration(PWM5, &PWMx_InitStructure);			//初始化PWM,  PWMA,PWMB

	// 配置PWMB
	PWMx_InitStructure.PWM_Period = period - 1;			//周期时间,   0~65535
	PWMx_InitStructure.PWM_DeadTime = 0;					//死区发生器设置, 0~255
	PWMx_InitStructure.PWM_MainOutEnable= ENABLE;			//主输出使能, ENABLE,DISABLE
	PWMx_InitStructure.PWM_CEN_Enable   = ENABLE;			//使能计数器, ENABLE,DISABLE
	PWM_Configuration(PWMB, &PWMx_InitStructure);			//初始化PWM通用寄存器,  PWMA,PWMB

	// 切换PWM通道
	PWM5_SW(PWM5_SW_P00);

	// 初始化PWMB的中断
	NVIC_PWM_Init(PWMB,DISABLE,Priority_0);
}

// 停止音乐
void Buzzer_Stop(void) {
    PWMB_CC5E_Disable();
}

// 初始化蜂鸣器
void Buzzer_Init(void) {
	EA = 1;			    // 使能全局中断
	EAXSFR();		    // 扩展寄存器访问使能

    Buzzer_GPIO();      // 初始化蜂鸣器引脚
    BUZZER = 0;         // 引脚先拉低,上电默认静音
    Buzzer_Uart();      // 初始化串口
    Buzzer_PWM(1000);   // 初始化PWM5, 1000Hz
    Buzzer_Stop();      // 关闭PWM5比较输出,等待按键播放后再响
}

// hz_value 音符频率
void Buzzer_Play(u16 hz_value) {
    u16 period = (MAIN_Fosc / hz_value);
    // Volume 0~10 对应占空比 0%~10%,整数运算避免浮点库占用 code
    u16 duty = (u16)((u32)period * Volume / 100);

    // printf("Buzzer_Play->Volume: %d, duty: %d\n", (int)Volume, duty);
    
    // 设置PWMB输出Period (1/频率)
    PWMB_AutoReload(period - 1);	//周期设置
    // 设置PWM5的占空比(蜂鸣器在 P00,走 PWM5)
    PWMB_Duty5(duty);
    // 启用PWMB通道5的输出使能
    PWMB_CC5E_Enable();
}

// 从当前 note_index 播一个音,并装入它的拍数
static void Buzzer_StartNote(void) {
    Buzzer_Play(hz[music[Song][note_index] - 1]);
    // 拍子换算:每单位 dur 对应 15 个 RTX tick(INT_CLOCK=10000,24MHz 12T 下约 5ms/tick,
    // 即 dur=4 的乐音约 300ms);note_remain 单位是任务循环轮数,故再除以 BUZZER_LOOP_TICKS
    note_remain = (u16)(music_dur[Song][note_index] * 15UL / BUZZER_LOOP_TICKS);
}

// 供外部查询:1=播放中(派生自 buzzer_state,不单独维护)
u8 Buzzer_IsPlaying(void) {
    return buzzer_state == BUZZER_PLAY;
}

// 音量改变后刷新当前正在发声的音符(仅播放/待暂停中有效)
void Buzzer_Refresh(void) {
    if (buzzer_state != BUZZER_PLAY && buzzer_state != BUZZER_PAUSING) return;
    Buzzer_Play(hz[music[Song][note_index] - 1]);
}

// 播放 / 暂停(非阻塞:只切换状态,不在这里循环)
void Buzzer_Play_Pause(u8 playing) {
    if (playing) {
        if (buzzer_state == BUZZER_PLAY) return;    // 已在播,忽略

        // 待暂停状态 -> 取消待暂停,继续播放
        if (buzzer_state == BUZZER_PAUSING) {
            buzzer_state = BUZZER_PLAY;
            return;
        }
        // 暂停状态 -> 从下一个音符继续播放(暂停时位置已推进到下一音)
        if (buzzer_state == BUZZER_PAUSE) {
            buzzer_state = BUZZER_PLAY;
            Buzzer_StartNote();     // 必须重装拍数:音尾暂停时 note_remain=0
            return;
        }
        // 停止状态 -> 从头播放 - 位置重置为0
        note_index = 0;
        buzzer_state = BUZZER_PLAY;
        Buzzer_StartNote();
    } else {
        // 播放中 -> 待暂停:当前音符播完后再暂停
        if (buzzer_state != BUZZER_PLAY) return;
        buzzer_state = BUZZER_PAUSING;
    }
}

// 每个任务循环调用一次,非阻塞推进播放
void Buzzer_Tick(void) {
    if (buzzer_state != BUZZER_PLAY && buzzer_state != BUZZER_PAUSING) return;

    if (note_remain > 0) {
        note_remain--;
        return;
    }

    note_index++;
    // 本曲播完
    if (note_index >= music_len[Song]) {
        if (buzzer_state == BUZZER_PAUSING) {
            // 结尾音播完才暂停:位置停在下一首开头,不出声
            Song++;
            if (Song > 2) Song = 0;
            note_index = 0;
            buzzer_state = BUZZER_PAUSE;
            Buzzer_Stop();
        } else {
            Buzzer_NextSong();      // 正常播放:自动连播下一首
        }
        return;
    }
    if (buzzer_state == BUZZER_PAUSING) {
        // 当前音符播完,暂停在下一个音符处,不出声
        buzzer_state = BUZZER_PAUSE;
        Buzzer_Stop();
        return;
    }
    Buzzer_StartNote();
}

// 按键2:切到下一首(任何状态都从下一首开始自动播放)
void Buzzer_NextSong(void) {
    Song++;
    if (Song > 2) Song = 0;
    note_index = 0;
    buzzer_state = BUZZER_PLAY;
    Buzzer_StartNote();
}


// 任务切换时调用:停止播放并清除所有播放状态,回归上电初始状态
void Buzzer_Clear(void) {
    Buzzer_Stop();
    buzzer_state = BUZZER_STOP;
    note_index  = 0;
    note_remain = 0;
    Volume      = 1;   // 初始音量,与上电默认一致
    Song        = 0;
}