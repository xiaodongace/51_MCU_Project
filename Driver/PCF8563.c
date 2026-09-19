#include "PCF8563.h"
#include "GPIO.h"
#include "I2C.h"
#include "NVIC.h"
#include "Switch.h"
#include "Exti.h"

/* 闹钟/定时器中断标志。INT3 中断服务里只置这一位，
 * 真正的 I2C 清标志动作（读 CS2、写回 AF=0）放到 TASK_LOGIC 里做，
 * 因为 I2C 总线要和副屏共用，不能在中断里抢。 */
volatile bit g_rtcIrqFlag = 0;

#define I2C_SOFT    0
#define I2C_WRITE   I2C_WriteNbyte
#define I2C_READ    I2C_ReadNbyte


static void GPIO_config(void) {
    GPIO_InitTypeDef	GPIO_InitStructure;				//结构定义

    GPIO_InitStructure.Pin  = GPIO_Pin_7;			//指定要初始化的IO, GPIO_Pin_0 ~ GPIO_Pin_7, 或操作
    GPIO_InitStructure.Mode = GPIO_PullUp;			//指定IO的输入或输出方式,GPIO_PullUp,GPIO_HighZ,GPIO_OUT_OD,GPIO_OUT_PP
    GPIO_Inilize(GPIO_P3,&GPIO_InitStructure);	//初始化
    
    // P32, P33配置为开漏输出模式
    P3_MODE_OUT_OD(GPIO_Pin_2 | GPIO_Pin_3);
}

/******************** INT配置 ********************/
void	Exti_config(void)
{
    EXTI_InitTypeDef	Exti_InitStructure;					//结构定义
    Exti_InitStructure.EXTI_Mode      = EXT_MODE_Fall;  //中断模式,   EXT_MODE_RiseFall,EXT_MODE_Fall
    Ext_Inilize(EXT_INT3,&Exti_InitStructure);				//初始化

    NVIC_INT3_Init(ENABLE,Priority_0);		                //中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

}


// 初始化PCF8563 (引脚和I2C)
void PCF8563_init(void){
    /* 修补：v3.1 把这两句注释掉了，导致 P3.2/P3.3 没配成开漏（I2C 直接不通）、
     * P3.7 没配成准双向口。江文聪老师封装驱动的 PCF8563_init() 是打开的，按它来。
     * I2C_config() 仍由 App_System.c 统一配置（本项目所有 I2C 从设备共用一套配置），
     * 所以这里只补 GPIO_config()。 */
    GPIO_config();
    Exti_config();
}

// 写日期和时间
void PCF8563_set_clock(Clock_t c){ // 值传递
    u8 C = 0;
    u8 p[NUMBER] = {0};    
    // 一次性写入时间日期信息 0x05 -> 0x50
    // 秒: VL 1 1 1 - 0 0 0 0  十进制数字->BCD  56 -> 0x56
    p[0] = ((c.second / 10) << 4) | (c.second % 10);  // (十位 << 4) | 个位
    // 分:  x 1 1 1 - 0 0 0 0  十进制数字->BCD  54 -> 0x54
    p[1] = ((c.minute / 10) << 4) | (c.minute % 10);  // (十位 << 4) | 个位
    // 时:  x x 1 1 - 0 0 0 0  十进制数字->BCD  23 -> 0x23
    p[2] = ((c.hour / 10) << 4)   | (c.hour % 10);   // (十位 << 4) | 个位
    
    // 日:  x x 1 1 - 0 0 0 0  十进制数字->BCD  31 -> 0x31
    p[3] = ((c.day / 10) << 4)    | (c.day % 10);  // (十位 << 4) | 个位
    // 周:  x x x x - x 0 0 0  
    p[4] = c.week;
    
    // 世纪
    C = (c.year >= 2100) ? 1 : 0;
    // 月:  C x x 1 - 0 0 0 0  十进制数字->BCD  12 -> 0x12
    p[5] = (C << 7) | ((c.month / 10) << 4) | (c.month % 10);  // (十位 << 4) | 个位

    // 2026 % 100 -> 26 / 10 -> 2
    // 年:  1 1 1 1 - 0 0 0 0  十进制数字->BCD  26 -> 0x26
    p[6] = ((c.year % 100 / 10) << 4) | (c.year % 10);  // (十位 << 4) | 个位

    // 写入: 秒, 分, 时, 日, 周, 月, 年, 世纪
    I2C_WRITE(PCF8563_ADDR, PCF8563_REG, p, NUMBER);
    
}

// 读日期和时间
void PCF8563_get_clock(Clock_t *p_clock){
    u8 p[NUMBER] = {0};    
    u8 C = 0;

    // 读取: 秒, 分, 时, 日, 周, 月, 年, 世纪
    I2C_READ(PCF8563_ADDR, PCF8563_REG, p, NUMBER);
    // 秒: VL 1 1 1 - 0 0 0 0  BCD转成十进制数字 -> 0xD6 -> 0x56 -> 56
    (*p_clock).second = ((p[0] >> 4) & 0x07) * 10 + (p[0] & 0x0F); // 十位 * 10 + 个位
    // 分:  x 1 1 1 - 0 0 0 0  BCD转成十进制数字  0x54 -> 54
    p_clock->minute = ((p[1] >> 4) & 0x07) * 10 + (p[1] & 0x0F); // 十位 * 10 + 个位
    // 时:  x x 1 1 - 0 0 0 0  BCD转成十进制数字  0x23 -> 23
    p_clock->hour   = ((p[2] >> 4) & 0x03) * 10 + (p[2] & 0x0F); // 十位 * 10 + 个位
    
    // 日:  x x 1 1 - 0 0 0 0  BCD转成十进制数字  0x31 -> 31
    p_clock->day    = ((p[3] >> 4) & 0x03) * 10 + (p[3] & 0x0F); // 十位 * 10 + 个位
    
    // 周:  x x x x - x 0 0 0  BCD转成十进制数字  0x31 -> 31
    p_clock->week   = p[4] & 0x07; // 0b0000 0111
    
    // 月:  C x x 1 - 0 0 0 0 BCD转成十进制数字  0x31 -> 31
    p_clock->month  = ((p[5] >> 4) & 0x01) * 10 + (p[5] & 0x0F);
    C      = p[5] >> 7; // 0->20xx年,  1->21xx年
    
    // 年: 1 1 1 1 - 0 0 0 0  BCD转成十进制数字  0x98 -> 98
    p_clock->year   = ((p[6] >> 4) & 0x0F) * 10 + (p[6] & 0x0F); // 十位 * 10 + 个位
    p_clock->year  += ((C == 0) ? 2000 : 2100); 
    
}

// 设置闹铃
void PCF8563_set_alarm(Alarm_t alarm){
    u8 a[4] = {0};    
    
//    3. 设置闹铃时间: 09h分钟, 0Ah小时, 0BH天, 0CH周(最高配置0: 启用)    
    // 分:  M 1 1 1 - 0 0 0 0  Enable -> 0x00, Disable -> 0x80 (1 << 7)
    if(alarm.minute < 0){
        a[0] = 0x80; // 禁用
    }else {
        a[0] = ((alarm.minute / 10) << 4) + (alarm.minute % 10) + 0x00;
    }
      
    // 时:  H x 1 1 - 0 0 0 0  Enable -> 0x00, Disable -> 0x80 (1 << 7)
    if(alarm.hour < 0){
        a[1] = 0x80; // 禁用
    }else { // 启用
        a[1] = ((alarm.hour / 10) << 4) + (alarm.hour % 10) + 0x00;
    }
      
    // 日:  D x 1 1 - 0 0 0 0  Enable -> 0x00, Disable -> 0x80 (1 << 7)
    if(alarm.day < 0){
        a[2] = 0x80;
    }else {
        a[2] = ((alarm.day / 10) << 4) + (alarm.day % 10) + 0x00;    
    }
      
    // 周:  M 1 1 1 - 0 0 0 0  Enable -> 0x00, Disable -> 0x80 (1 << 7)
    if(alarm.week < 0){
        a[3] = 0x80;
    }else {
        a[3] = alarm.week + 0x00;
    }
    
    I2C_WRITE(PCF8563_ADDR, 0x09, a, 4);
    
}

// 启用闹铃
// 0b 0000 0010
// 0b 0000 0000
void PCF8563_enable_alarm(u8 enable){
    u8 cs2 = 0;
    I2C_READ(PCF8563_ADDR, 0x01, &cs2, 1);
    // AF  -> 清理Alarm标记 Bit3 Alarm Flag 清0  确保闹铃可以触发
    cs2 &= ~( 1 << 3 );
    // AIE -> 开启Alarm中断 Bit1 Alarm Interrupt Enable 
    if(enable){
        cs2 |=   ( 1 << 1 );   
    }else {
        cs2 &=  ~( 1 << 1 );   
    }
    
    I2C_WRITE(PCF8563_ADDR, 0x01, &cs2, 1);
}

// 清理闹铃标记
void PCF8563_clear_alarm(){
    u8 cs2 = 0;
//    4. 配置控制寄存器2, CS2 , AF=0, AIE=1启用闹铃
    I2C_READ(PCF8563_ADDR, 0x01, &cs2, 1);
    // AF  -> 清理Alarm标记 Bit3 Alarm Flag 清0  确保闹铃可以触发
    cs2 &= ~( 1 << 3 );
    // 将CS2配置信息写到PCF8563的0x01开始的1个寄存器
    I2C_WRITE(PCF8563_ADDR, 0x01, &cs2, 1);
}

// 设置定时器(频率 & 计数值)
void PCF8563_set_timer(TimerFreq freq, u8 countdown){
    //3. 设置Timer运行频率 & 启用Timer
    u8 p;
    p = (1 << 7) | freq;    // 4096Hz, 64Hz, 1Hz, 1/60Hz
    I2C_WRITE(PCF8563_ADDR, 0x0E, &p, 1);

    //4. 设置Timer计数值
    p = countdown;
    I2C_WRITE(PCF8563_ADDR, 0x0F, &p, 1);
    
}

// 启用定时器Timer
void PCF8563_enable_timer(u8 enable){
    u8 cs2 = 0;
    I2C_READ(PCF8563_ADDR, 0x01, &cs2, 1);
    // TF  -> 清理Timer标记 Bit2 Timer Flag 清0  确保Timer可以触发
    cs2 &= ~( 1 << 2 );
    // TIE -> 开启Timer中断 Bit0 Timer Interrupt Enable
    if(enable){
        cs2 |=  ( 1 << 0 );
    }else {
        cs2 &= ~( 1 << 0 );    
    }
    // 写回去
    I2C_WRITE(PCF8563_ADDR, 0x01, &cs2, 1);
}

// 清理定时器Timer标记
void PCF8563_clear_timer(){
    u8 cs2 = 0;
    // 6. 启用外部中断, 在中断里判断Timer并清理TF标记
    I2C_READ(PCF8563_ADDR, 0x01, &cs2, 1);
    // TF  -> 清理Timer标记 Bit2 Timer Flag 清0  确保Timer可以触发
    cs2 &= ~( 1 << 2 );
    // 写回去
    I2C_WRITE(PCF8563_ADDR, 0x01, &cs2, 1);
}

// 谁调用此函数
void PCF8563_int_call(void) {
    
// 通过宏, 对代码进行裁剪
#if PCF8563_ALARM_ENABLE || PCF8563_TIMER_ENABLE
    
    // 0. 先将 CS2 寄存器值读出来
    u8 cs2 = 0;
    // 读取CS2寄存器, 用于区分是Alarm还是Timer触发的中断
    I2C_READ(PCF8563_ADDR, 0x01, &cs2, 1);

    #if PCF8563_ALARM_ENABLE
    // 1. 判断AF(Bit3) && AIE(Bit1)
    if(((cs2 >> 3) & 1) && ((cs2 >> 1) & 1)) {
        // 清理alarm标记
//        PCF8563_clear_alarm();
        cs2 &= ~( 1 << 3 );

        // 具体的业务逻辑由用户决定
        PCF8563_on_alarm();
    }
    #endif

    #if PCF8563_TIMER_ENABLE
    // 2. 判断TF(Bit2) && TIE(Bit0)
    if((cs2 & (1 << 2)) && (cs2 & (1 << 0))) {
        // 清理timer标记
//        PCF8563_clear_timer();
        cs2 &= ~( 1 << 2 );
        
        // 具体的业务逻辑由用户决定
        PCF8563_on_timer();
    }
    #endif
    
    // 将CS2配置信息写到PCF8563的0x01开始的1个寄存器
    I2C_WRITE(PCF8563_ADDR, 0x01, &cs2, 1);
    
#endif
}
