#include "App_Menu.h"
#include "Buzzer.h"
#include "Key.h"
#include "LED.h"


/* 按键编号 */
#define MENU_KEY_NEXT 0
#define MENU_KEY_PREVIOUS 1
#define MENU_KEY_ENTER 2
#define MENU_KEY_BACK 3

/* SPI屏幕每次显示4个16像素高的菜单项目 */
#define MENU_VISIBLE_ROWS 4

/* 当前菜单选中的项目 */
volatile u8 menu_selected = MENU_ITEM_ALARM;

/* 当前页面，开机默认处于菜单 */
volatile u8 current_page = PAGE_MENU;

/*
 * 菜单中文使用GB2312编码。
 * 使用字节数组避免源码文件编码不同导致乱码。
 */
/* 菜单1：“闹钟” */
static u8 code menu_text_alarm[] = {
    0xC4, 0xD6, 0xD6, 0xD3, 0};

/* 菜单2：“音乐盒” */
static u8 code menu_text_music_box[] = {
    0xD2, 0xF4, 0xC0, 0xD6, 0xBA, 0xD0, 0};

/* 菜单3：“电子琴” */
static u8 code menu_text_piano[] = {
    0xB5, 0xE7, 0xD7, 0xD3, 0xC7, 0xD9, 0};

/* 菜单4：“温湿度采集” */
static u8 code menu_text_dht11[] = {
    0xCE, 0xC2, 0xCA, 0xAA, 0xB6, 0xC8,
    0xB2, 0xC9, 0xBC, 0xAF, 0};

/* 菜单5：“马达示波器” */
static u8 code menu_text_motor_scope[] = {
    0xC2, 0xED, 0xB4, 0xEF, 0xCA, 0xBE,
    0xB2, 0xA8, 0xC6, 0xF7, 0};

/* 菜单选中箭头 */
static u8 code menu_arrow[] = {
    '>', 0};

/*
 * 在指定位置显示一个菜单项目
 */
static void Menu_ShowItem(
    u8 item_index,
    u8 y) {
    switch (item_index) {
    case MENU_ITEM_ALARM:
        SPI_OLED_Display_GB2312_string(
            16,
            y,
            menu_text_alarm);
        break;

    case MENU_ITEM_MUSIC_BOX:
        SPI_OLED_Display_GB2312_string(
            16,
            y,
            menu_text_music_box);
        break;

    case MENU_ITEM_PIANO:
        SPI_OLED_Display_GB2312_string(
            16,
            y,
            menu_text_piano);
        break;

    case MENU_ITEM_DHT11:
        SPI_OLED_Display_GB2312_string(
            16,
            y,
            menu_text_dht11);
        break;

    case MENU_ITEM_MOTOR_SCOPE:
        SPI_OLED_Display_GB2312_string(
            16,
            y,
            menu_text_motor_scope);
        break;
    }
}

/*
 * 刷新SPI屏幕菜单。
 * 128x64屏幕使用16像素字体时一次显示4行。
 */
void Menu_Refresh(void) {
    u8 first_item;
    u8 row;
    u8 item_index;
    u8 y;

    /*
     * 计算当前菜单窗口的第一项。
     * 选中后面项目时菜单自动向下滚动。
     */
    if (menu_selected < MENU_VISIBLE_ROWS)
        first_item = 0;
    else
        first_item =
            menu_selected - MENU_VISIBLE_ROWS + 1;

    /* 清空SPI OLED */
    SPI_OLED_Clear();

    /*
     * 绘制当前窗口中的4个菜单项目
     */
    for (row = 0; row < MENU_VISIBLE_ROWS; row++) {
        item_index = first_item + row;

        /*
         * 防止超过菜单总数
         */
        if (item_index >= MENU_ITEM_COUNT)
            break;

        /*
         * 16像素高字体占用两个OLED页
         */
        y = row * 2;

        /*
         * 当前选中项显示箭头
         */
        if (item_index == menu_selected) {
            SPI_OLED_Display_GB2312_string(
                0,
                y,
                menu_arrow);
        }

        /* 显示菜单文字 */
        Menu_ShowItem(item_index, y);
    }
}

/*
 * 根据当前选中的菜单进入对应页面
 */
static void Menu_EnterSelected(void) {
    switch (menu_selected) {
    case MENU_ITEM_ALARM:
        /* 闹钟功能暂未接入，先保留独立页面状态 */
        current_page = PAGE_ALARM;
        break;

    case MENU_ITEM_MUSIC_BOX:
        /*
         * 音乐盒
         * 进入前清除上次的播放状态 避免继续上次的音符
         */
        Buzzer_Clear();

        /* 音乐停止时同时关闭全部LED */
        LED_AllOff();

        /* 切换按键归属 后续四个按键交给音乐盒功能 */
        current_page = PAGE_BUZZER;
        break;

    case MENU_ITEM_PIANO:
        /* 电子琴 */
        current_page = PAGE_PIANO;
        break;

    case MENU_ITEM_DHT11:
        /*
         * 切换到温湿度采集页面
         * DHT11任务检测到该状态后才会初始化并刷新I2C OLED
         */
        current_page = PAGE_DHT11;
        break;

    case MENU_ITEM_MOTOR_SCOPE:
        /* 马达示波器 */
        current_page = PAGE_MOTOR_SCOPE;
        break;
    }
}

/*
 * 退出当前功能并返回菜单
 */
static void Menu_ExitCurrentPage(void) {
    u8 old_page;

    /* 保存退出前的页面 */
    old_page = current_page;

    /*
     * 先恢复菜单状态，
     * 其他任务下一次运行时会停止功能刷新
     */
    current_page = PAGE_MENU;

    /*
     * 如果退出的是蜂鸣器页面
     * 通知蜂鸣器任务停止播放
     */
    if (old_page == PAGE_BUZZER) {
        /* 停止播放并清除蜂鸣器内部状态 */
        Buzzer_Clear();

        /* 退出蜂鸣器页面后关闭全部LED */
        LED_AllOff();
    }

    /*
     * 重新刷新SPI菜单
     */
    Menu_Refresh();
}

/*
 * 处理菜单页面的按键
 */
static void Menu_HandleKeys(
    u8 k1,
    u8 k2,
    u8 k3) {
    /*
     * K1：选择下一个菜单
     */
    if (k1) {
        menu_selected++;

        /*
         * 到达最后一项后循环回到第一项
         */
        if (menu_selected >= MENU_ITEM_COUNT)
            menu_selected = 0;

        Menu_Refresh();
    }

    /*
     * K2：选择上一个菜单
     */
    if (k2) {
        /*
         * 在第一项继续向上时，
         * 循环到最后一项
         */
        if (menu_selected == 0)
            menu_selected = MENU_ITEM_COUNT - 1;
        else
            menu_selected--;

        Menu_Refresh();
    }

    /*
     * K3：进入当前选中的功能
     */
    if (k3) {
        Menu_EnterSelected();
    }
}

/*
 * 处理蜂鸣器页面的按键
 */
static void Menu_HandleBuzzerKeys(
    u8 k1,
    u8 k2,
    u8 k3,
    u8 k4_short,
    u8 k4_long) {
    /*
     * 长按K4退出优先级最高
     */
    if (k4_long) {
        Menu_ExitCurrentPage();
        return;
    }

    /*
     * K1：播放或暂停
     */
    if (k1) {
        /* 根据当前播放状态切换播放或暂停 */
        Buzzer_Play_Pause(!Buzzer_IsPlaying());
    }

    /*
     * K2：下一首音乐
     */
    if (k2) {
        /* 切换到下一首音乐并开始播放 */
        Buzzer_NextSong();
    }

    /*
     * K3：增加音量
     */
    if (k3) {
        /* 音量最大限制为10，防止越界 */
        if (Volume < 10)
            Volume++;

        /* 立即刷新当前正在发声的音符音量 */
        Buzzer_Refresh();
    }

    /*
     * K4短按：减少音量
     */
    if (k4_short) {
        /* 音量最小限制为0，防止越界 */
        if (Volume > 0)
            Volume--;

        /* 立即刷新当前正在发声的音符音量 */
        Buzzer_Refresh();
    }
}

/*
 * 菜单和按键总任务
 */
/* 按Keil C51 RTX51任务入口格式定义，确保生成任务表记录 */
void App_Menu_Task() _task_ App_Menu_Task_Id {
    u8 k1_press;
    u8 k2_press;
    u8 k3_press;
    u8 k4_short;
    u8 k4_long;

    /*
     * 菜单任务负责初始化SPI屏幕，
     * 这样不需要再创建原来的SPI_OLED_Task。
     */
//    SPI_OLED_Init();

//    /* 使用正常显示颜色 */
//    SPI_OLED_ColorTurn(0);

//    /* 使用正常显示方向 */
//    SPI_OLED_DisplayTurn(0);

    /* 开机绘制第一次菜单 */
    Menu_Refresh();

    while (1) {
        /*
         * 整个工程只能在这里调用Key_Scan()
         */
        Key_Scan();

        /*
         * K1、K2、K3使用按下事件，
         * 按下后立即响应
         */
        k1_press = Key_GetPressEvent(0);
        k2_press = Key_GetPressEvent(1);
        k3_press = Key_GetPressEvent(2);

        /*
         * 消费K4普通按下事件，
         * 但不直接使用，避免长按时误触发短按功能
         */
        Key_GetPressEvent(3);

        /*
         * K4使用短按和长按事件
         */
        k4_short = Key_GetShortPressEvent(3);
        k4_long = Key_GetLongPressEvent(3);

        /*
         * 当前处于菜单页面
         */
        if (current_page == PAGE_MENU) {
            Menu_HandleKeys(
                k1_press,
                k2_press,
                k3_press);

            /*
             * 菜单页面忽略K4短按和长按
             */
        }
        /*
         * 当前处于蜂鸣器页面
         */
        else if (current_page == PAGE_BUZZER) {
            Menu_HandleBuzzerKeys(
                k1_press,
                k2_press,
                k3_press,
                k4_short,
                k4_long);
        }
        /*
         * 当前处于其他功能页面
         */
        else {
            /*
             * 其他功能不使用独立按键
             * 只保留K4长按退出
             */
            if (k4_long) {
                Menu_ExitCurrentPage();
            }
        }

        /*
         * 每个RTX节拍扫描一次按键
         */
        os_wait2(K_TMO, 1);
    }
}
