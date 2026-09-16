#include "NIXIE.h"
#include "Delay.h"

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
Nixie_init(){
		NIXIE_PIN_INIT();
		Timer_config();
}

// 在某一位显示数字
Nixie_show(num, idx){

}

// 显示函数
Nixie_display(num, idx){

}

// 扫描
Nixie_Scan2ms(){
		
}