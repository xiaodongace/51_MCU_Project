#include "DHT_11.h"


void I2C_GPIO(void)
{
    P4_MODE_IO_PU(GPIO_Pin_6);
    P5_MODE_OUT_OD(GPIO_Pin_3);
    P3_MODE_OUT_OD(GPIO_Pin_2 | GPIO_Pin_3);
    P3_MODE_IO_PU(GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_7);
}



// 初始化P3引脚
int8 on_read_dht11(u8 dat[]){
    u16 data cnt = 0; // 计数器, 每+1, 代表时间过了1us
    int8 i, j;

    // 1、主机发起起始信号: 拉低 18ms, 30ms
    DHT = 0;
    os_wait2(K_TMO, 4);

    DHT = 1;

    // 2、主机释放总线 (13us, 35us)
	cnt = 0; // 确保开始是0, 同时, 也让DHT有时间真正拉起来
    while(DHT == 1 && cnt < 45) {
        // 每循环一次,代表过去了1us,通过cnt记录时间
        delay_1us();
        cnt++;
    }
    // 如果不符合目标范围, 及时短路返回, 避免代码嵌套
    if(cnt < 6 || cnt > 35) {
        printf("err: 时间[%dus], 不满足 主机释放总线时间[%dus, %dus]\n", cnt, (int)6, (int)35); 
        return -1;
    }

    // 3、响应低电平时间 83us, [78, 88]us, 当前0, 直到1, 结束循环
    wait_level_change(0, 78, 88, "响应低电平时间");

    // 4、响应高电平时间 87us, [78, 95]us, 当前1, 直到0, 结束循环
    wait_level_change(1, 78, 95, "响应高电平时间");

    // 5、解析40bit的数据(5Byte * 8bit)
    // 外循环: 1次, 接收处理1个byte字节(一共5个字节)
    for(i = 0; i < 5; i++){ // 0,1,2,3,4

        // 内循环: 1次, 接收处理1个bit位(每个字节8bit)
        for(j = 7; j >= 0; j--){ // 7,6,5,4,2,1,0 先收到高位
            // 一个bit信号由一低一高的电平组成: 低电平一样长(54us), 区别在于高电平

            // 数据信号: 低电平时间 54us [50, 58]us 当前0, 直到1
            wait_level_change(0, 40, 62, "Data信号低电平时间");

            // 数据信号: 高电平时间 [23, 74]us 当前1, 直到0
            wait_level_change(1, 20, 74, "Data信号高电平时间");

            // 通过高电平时长cnt, 区分是0还是1 (是0就不管, 默认dat存的都是0)
            // (24 + 71) / 2 = 47.5

            // 信号1: 指定置1
            if(cnt > 47) {
                dat[i] |= ( 1 << j ); 
            }
        }
    }

    // 主机拉高释放总线(可选)
    DHT = 1;

    printf("cnt -> %d us\n", cnt);
    // 打印5个字节的数据
    printf("dat-> ");
    for(i = 0; i < 5; i++){
        printf("%d ", (int)dat[i]);
    }
    printf("\n");
    
    // 校验数据: 8bit 湿度整数数据 + 8bit 湿度小数数据 + 8bit 温度整数数据 + 8bit 温度小数数据”8bit 校验位等于所得结果的末 8 位。
    if(((dat[0] + dat[1] + dat[2] + dat[3]) & 0xFF) != dat[4]){
        printf("校验失败: %d!\n", (int)__LINE__);
        return -3;
    }
    
    printf("校验通过: %d!\n", (int)__LINE__);

    return 0;
}


void DHT11_Init() {
    // P4M0 &= ~0x1c; P4M1 |= 0x1c;
    I2C_GPIO();
}

int8 DHT11_get_info(float* p_humidity, float* p_temperature) {
    float humidity; // 湿度
    float temperature; // 温度    
    int8 rst; // rst -> result 
    u8 dat[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    
    rst = on_read_dht11(dat);

    if(rst != SUCCESS){
        printf("读取温湿度信息失败, 错误码: %d\n", (int) rst);
        return rst; 
    }
    // 湿度高8位为 整数部分数据
    humidity = dat[0];

    // 模拟负温度
    // dat[3] |= (1 << 7);

    // 温度高8位 整数部分, 低8位 小数部分
    // 整数部分 + 小数部分(低7位) * 0.1
    temperature = dat[2] + (dat[3] & 0x7F) * 0.1f;

    // 如果温度最高位是1, 表示温度为负
    if((dat[3] >> 7) & 0x01){ // dat[3] & 0x80 == 0x80
        temperature *= -1;    // 取反, 变负数
    }
    *p_humidity = humidity;
    *p_temperature = temperature;
    
    return rst; 
}