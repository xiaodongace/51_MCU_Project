#include "App_Public.h"


/* DHT11建议的采样间隔，单位为毫秒 */
#define DHT11_SAMPLE_INTERVAL_MS 1000

/* DHT11任务空闲检查周期，单位为RTX51节拍 */
#define DHT11_IDLE_TICKS 2

/*
 * 绘制温湿度页面中不变化的标题。
 * 中文字库序号沿用原工程：0、1为“湿度”，2、3为“温度”。
 */
static void DHT11_ShowFrame(void) {
    u8 x;

    /* 进入页面时先清除I2C OLED原有内容 */
    I2C_OLED_Clear();

    /* 第一行显示“湿度：” */
    x = 0;
    x = I2C_OLED_ShowChinese(x, 0, 0, 16);
    x = I2C_OLED_ShowChinese(x, 0, 1, 16);
    I2C_OLED_ShowChar(x, 0, ':', 16);

    /* 第二行显示“温度：” */
    x = 0;
    x = I2C_OLED_ShowChinese(x, 2, 2, 16);
    x = I2C_OLED_ShowChinese(x, 2, 3, 16);
    I2C_OLED_ShowChar(x, 2, ':', 16);

    /* 第三行显示“电压：” */
    x = 0;
    x = I2C_OLED_ShowChinese(x, 4, 5, 16);
    x = I2C_OLED_ShowChinese(x, 4, 6, 16);
    I2C_OLED_ShowChar(x, 4, ':', 16);
    
    /* 第四行显示“电阻：” */
    x = 0;
    x = I2C_OLED_ShowChinese(x, 6, 9, 16);
    x = I2C_OLED_ShowChinese(x, 6, 10, 16);
    I2C_OLED_ShowChar(x, 6, ':', 16);
}

/*
 * 把最新的DHT11数据写入I2C OLED。
 * 字符串使用固定宽度，防止新数据较短时残留旧字符。
 */
static void DHT11_ShowData(
    float humidity,
    float temperature,
    float vol,
    float ntc) {
    u8 x;
    char hum_buf[12];
    char temp_buf[12];
    char vol_buf[12];
    char ntc_buf[12];

    /* 格式化湿度和温度，末尾空格用于覆盖旧内容 */
    sprintf(hum_buf, "%6.2f%% ", humidity);
    sprintf(temp_buf, "%6.2f ", temperature);
    sprintf(vol_buf, "%5.2f ", vol);
    sprintf(ntc_buf, "%6.2f ", ntc);

    /* 更新湿度值 */
    I2C_OLED_ShowString(40, 0, hum_buf, 16);

    /* 更新温度值，并在数值后显示摄氏度符号 */
    x = I2C_OLED_ShowString(40, 2, temp_buf, 16);
    I2C_OLED_ShowChinese(x, 2, 4, 16);

    /* 更新电压值，数值从固定列40开始，单位用字符V */
    x = I2C_OLED_ShowString(40, 4, vol_buf, 16);
    I2C_OLED_ShowChar(x, 4, 'V', 16);

    /* 更新电阻值，数值从固定列40开始，单位用字库11号Ω */
    x = I2C_OLED_ShowString(40, 6, ntc_buf, 16);
    I2C_OLED_ShowChinese(x, 6, 11, 16);
}

/*
 * DHT11读取失败时显示错误提示。
 * 下一次读取成功后，固定宽度数据会覆盖该提示。
 */
static void DHT11_ShowError(void) {
    I2C_OLED_ShowString(40, 0, "ERROR   ", 16);
    I2C_OLED_ShowString(40, 2, "ERROR   ", 16);
}

/*
 * DHT11温湿度任务。
 * 任务始终存在，但只有进入PAGE_DHT11页面后才初始化和采集数据。
 */
void App_DHT11_OLED_Task(void) _task_ App_DHT11_Task_Id {
    u8 page_active;
    u16 now;
    u16 last_sample_ms;
    float humidity;
    float temperature;
    float vol, ntc;
    u16 adc_value;
    int8 result;

    /* 开机默认没有进入DHT11页面 */
    page_active = 0;
    last_sample_ms = 0;

    while (1) {
        /* 当前没有进入温湿度页面 */
        if (current_page != PAGE_DHT11) {
            /* 从温湿度页面退出时关闭I2C OLED */
            if (page_active) {
                I2C_OLED_Clear();
                I2C_OLED_Display_Off();
                page_active = 0;
            }

            /* 空闲等待，不采集DHT11，也不访问I2C OLED */
            os_wait2(K_TMO, DHT11_IDLE_TICKS);
            continue;
        }

        /* 第一次进入温湿度页面时初始化相关硬件 */
        if (!page_active) {
            /* 初始化DHT11单总线引脚 */
            DHT11_Init();

            /* 初始化热敏电阻引脚与ADC模块 */
            NTC_init();

            /* 初始化并开启I2C OLED */
            I2C_OLED_Init();
            I2C_OLED_ColorTurn(0);
            I2C_OLED_DisplayTurn(0);
            I2C_OLED_Display_On();

            /* 绘制温湿度页面标题 */
            DHT11_ShowFrame();

            /* 允许进入页面后立即执行第一次采样 */
            last_sample_ms =
                Timers_GetSystemMs() -
                DHT11_SAMPLE_INTERVAL_MS;
            page_active = 1;
        }

        /* 使用系统毫秒计时控制DHT11采样频率 */
        now = Timers_GetSystemMs();
        if ((u16)(now - last_sample_ms) >=
            DHT11_SAMPLE_INTERVAL_MS) {
            last_sample_ms = now;

            /* 读取DHT11湿度和温度 */
            result = DHT11_get_info(
                &humidity,
                &temperature);
                
            // 电压
            adc_value = Get_ADCResult(ADC_CH13);
            vol = adc_value * 2.5 / 4096;
            // 计算热敏电阻温度
            ntc = NTC_get_temperature();

            /* 读取成功时更新数据，失败时显示错误提示 */
            if (result == SUCCESS)
                DHT11_ShowData(humidity, temperature, vol, ntc);
            else
                DHT11_ShowError();
        }

        /* 短周期让出CPU，使长按K4退出能够及时生效 */
        os_wait2(K_TMO, DHT11_IDLE_TICKS);
    }
}
