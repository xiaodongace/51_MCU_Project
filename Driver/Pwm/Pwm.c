#include "Pwm.h"

/*
 * 保存驱动层当前配置的公共周期。
 *
 * Pwm_SetDuty() 需要知道当前计数器的周期，才能检查占空比是否越界。
 * PWMA 和 PWMB 各自只有一个公共周期，因此只需要保存两个值。
 */
static u16 Pwm_PeriodA = 0;
static u16 Pwm_PeriodB = 0;

/*
 * 函数功能：检查指定 PWM 通道的引脚复用编号是否有效。
 *
 * 参数：
 *     channel - PWM1 ~ PWM8。
 *     route   - 引脚复用编号，取值范围由具体 PWM 通道决定。
 *
 * 返回值：
 *     SUCCESS - 路由编号有效。
 *     FAIL    - 路由编号超出该 PWM 通道支持的范围。
 *
 * 说明：
 *     PWM1、PWM2、PWM3 各支持 0 ~ 2 三种路由；
 *     PWM4、PWM5、PWM6、PWM7、PWM8 各支持 0 ~ 3 四种路由。
 */
static u8 Pwm_RouteValid(u8 channel, u8 route)
{
    if ((channel == PWM1) ||
        (channel == PWM2) ||
        (channel == PWM3))
    {
        if (route > 2)
        {
            return FAIL;
        }
    }
    else
    {
        if (route > 3)
        {
            return FAIL;
        }
    }

    return SUCCESS;
}


/*
 * 函数功能：根据 PWM 通道设置对应的引脚复用寄存器。
 *
 * 参数：
 *     channel - PWM1 ~ PWM8。
 *     route   - 具体通道的引脚复用编号。
 *
 * 返回值：
 *     无。
 *
 * 说明：
 *     不同 PWM 通道使用不同的 PWMx_SW 宏。
 *     这里集中处理通道和路由之间的对应关系，业务驱动不需要直接操作
 *     PWMA_PS 或 PWMB_PS 寄存器。
 */
static void Pwm_SelectRoute(u8 channel, u8 route)
{
    switch (channel)
    {
        case PWM1:
            PWM1_SW(route);
            break;

        case PWM2:
            PWM2_SW(route);
            break;

        case PWM3:
            PWM3_SW(route);
            break;

        case PWM4:
            PWM4_SW(route);
            break;

        case PWM5:
            PWM5_SW(route);
            break;

        case PWM6:
            PWM6_SW(route);
            break;

        case PWM7:
            PWM7_SW(route);
            break;

        case PWM8:
            PWM8_SW(route);
            break;

        default:
            break;
    }
}


/*
 * 函数功能：获取指定 PWM 通道所属的公共计数器。
 *
 * 参数：
 *     channel - PWM1 ~ PWM8。
 *
 * 返回值：
 *     PWMA - PWM1 ~ PWM4 所属的公共计数器。
 *     PWMB - PWM5 ~ PWM8 所属的公共计数器。
 */
static u8 Pwm_GetTimer(u8 channel)
{
    if (channel <= PWM4)
    {
        return PWMA;
    }

    return PWMB;
}


/*
 * 函数功能：初始化一个 PWM 通道及其所属的公共计数器。
 *
 * 参数：
 *     config - PWM 初始化配置结构体指针。
 *
 * 返回值：
 *     SUCCESS - 初始化成功。
 *     FAIL    - 参数非法，或者底层 PWM/中断初始化失败。
 *
 * 处理流程：
 *     1. 检查通道、周期、占空比和路由参数。
 *     2. 将通用配置转换为 STC 官方库的 PWMx_InitDefine 结构体。
 *     3. 调用 PWM_Configuration(channel, ...) 配置具体通道。
 *     4. 调用 PWM_Configuration(PWMA/PWMB, ...) 配置公共计数器。
 *     5. 设置 PWM 输出引脚复用。
 *     6. 配置 PWM 中断状态和优先级。
 *
 * 注意：
 *     PWM5 和 PWM6 都属于 PWMB，因此它们共用 Period。
 *     这个函数可以统一代码，但不能改变芯片的硬件资源共享关系。
 */
u8 Pwm_Init(Pwm_InitTypeDef *config)
{
    PWMx_InitDefine pwmInit;
    u8 timer;

    /* 配置指针为空时不能继续访问结构体成员。 */
    if (config == NULL)
    {
        return FAIL;
    }

    /* 当前 STC8H_PWM 库定义的有效输出通道为 PWM1 ~ PWM8。 */
    if ((config->Channel < PWM1) ||
        (config->Channel > PWM8))
    {
        return FAIL;
    }

    /* 周期为 0 没有有效的 PWM 计数范围。 */
    if (config->Period == 0)
    {
        return FAIL;
    }

    /* 比较值不能大于自动重装载值。 */
    if (config->Duty > config->Period)
    {
        return FAIL;
    }

    /* 提前检查路由，避免底层寄存器被部分修改。 */
    if (Pwm_RouteValid(
            config->Channel,
            config->Route
        ) != SUCCESS)
    {
        return FAIL;
    }

    /* PWM1~PWM4 使用 PWMA，PWM5~PWM8 使用 PWMB。 */
    timer = Pwm_GetTimer(config->Channel);

    /*
     * 这里将每个成员都明确赋值。
     * 原工程中部分 PWMx_InitStructure 成员没有在第一次使用前赋值，
     * 可能会把栈中的随机值带入底层函数。统一初始化后更加安全。
     */
    pwmInit.PWM_Mode          = config->Mode;
    pwmInit.PWM_Period        = config->Period;
    pwmInit.PWM_Duty          = config->Duty;
    pwmInit.PWM_DeadTime      = config->DeadTime;
    pwmInit.PWM_EnoSelect     = config->OutputSelect;
    pwmInit.PWM_CEN_Enable    = config->CounterEnable;
    pwmInit.PWM_MainOutEnable = config->MainOutputEnable;

    /* 配置 PWM5/PWM6 等具体比较通道。 */
    if (PWM_Configuration(
            config->Channel,
            &pwmInit
        ) != SUCCESS)
    {
        return FAIL;
    }

    /* 配置 PWMA 或 PWMB 的公共周期、计数器和主输出。 */
    if (PWM_Configuration(
            timer,
            &pwmInit
        ) != SUCCESS)
    {
        return FAIL;
    }

    /* 设置 PWM 通道的引脚复用位置。 */
    Pwm_SelectRoute(
        config->Channel,
        config->Route
    );

    /* 配置公共 PWM 定时器的中断状态和优先级。 */
    if (NVIC_PWM_Init(
            timer,
            config->InterruptState,
            config->Priority
        ) != SUCCESS)
    {
        return FAIL;
    }

    /* 只有底层初始化全部成功后，才更新驱动层保存的周期。 */
    if (timer == PWMA)
    {
        Pwm_PeriodA = config->Period;
    }
    else
    {
        Pwm_PeriodB = config->Period;
    }

    return SUCCESS;
}


/*
 * 函数功能：修改指定 PWM 通道的占空比。
 *
 * 参数：
 *     channel - PWM1 ~ PWM8。
 *     duty    - 要写入比较寄存器的比较值。
 *
 * 返回值：
 *     SUCCESS - 占空比写入成功。
 *     FAIL    - PWM 通道编号无效。
 *
 * 说明：
 *     此函数只修改比较值，不修改 PWM 周期、引脚复用和输出使能状态。
 */
u8 Pwm_SetDuty(u8 channel, u16 duty)
{
    /*
     * PWM1~PWM4 共用 PWMA，PWM5~PWM8 共用 PWMB。
     * 占空比不能大于对应公共计数器的周期。
     */
    if (channel <= PWM4)
    {
        if ((Pwm_PeriodA == 0) ||
            (duty > Pwm_PeriodA))
        {
            return FAIL;
        }
    }
    else if ((channel >= PWM5) &&
             (channel <= PWM8))
    {
        if ((Pwm_PeriodB == 0) ||
            (duty > Pwm_PeriodB))
        {
            return FAIL;
        }
    }
    else
    {
        return FAIL;
    }

    switch (channel)
    {
        case PWM1:
            PWMA_Duty1(duty);
            break;

        case PWM2:
            PWMA_Duty2(duty);
            break;

        case PWM3:
            PWMA_Duty3(duty);
            break;

        case PWM4:
            PWMA_Duty4(duty);
            break;

        case PWM5:
            PWMB_Duty5(duty);
            break;

        case PWM6:
            PWMB_Duty6(duty);
            break;

        case PWM7:
            PWMB_Duty7(duty);
            break;

        case PWM8:
            PWMB_Duty8(duty);
            break;

        default:
            return FAIL;
    }

    return SUCCESS;
}


/*
 * 函数功能：按照目标频率修改 PWMA 或 PWMB 的公共周期。
 *
 * 参数：
 *     timer     - PWMA 或 PWMB。
 *     frequency - 目标频率，单位为 Hz。
 *
 * 返回值：
 *     SUCCESS - 周期设置成功。
 *     FAIL    - 定时器无效、频率为 0 或周期超出 16 位寄存器范围。
 *
 * 计算方式：
 *     period = MAIN_Fosc / frequency;
 *     自动重装载值 = period - 1;
 *
 * 注意：
 *     PWMA 下的 PWM1~PWM4 共用 PWMA 周期；
 *     PWMB 下的 PWM5~PWM8 共用 PWMB 周期。
 */
u8 Pwm_SetFrequency(u8 timer, u32 frequency)
{
    u32 period;

    if ((timer != PWMA) &&
        (timer != PWMB))
    {
        return FAIL;
    }

    if (frequency == 0)
    {
        return FAIL;
    }

    /* 使用 32 位计算，避免 MAIN_Fosc 或中间结果溢出。 */
    period = (u32)MAIN_Fosc / frequency;

    /* 自动重装载寄存器是 16 位，允许的计数值为 1~65536。 */
    if ((period == 0) ||
        (period > 65536UL))
    {
        return FAIL;
    }

    period--;

    if (timer == PWMA)
    {
        PWMA_AutoReload((u16)period);
        Pwm_PeriodA = (u16)period;
    }
    else
    {
        PWMB_AutoReload((u16)period);
        Pwm_PeriodB = (u16)period;
    }

    return SUCCESS;
}


/*
 * 函数功能：打开指定 PWM 通道的比较输出。
 *
 * 参数：
 *     channel - PWM1 ~ PWM8。
 *
 * 返回值：
 *     SUCCESS - 通道输出已打开。
 *     FAIL    - PWM 通道编号无效。
 *
 * 说明：
 *     这个函数只打开指定通道，不会修改其他 PWM 通道的输出状态。
 */
u8 Pwm_Enable(u8 channel)
{
    switch (channel)
    {
        case PWM1:
            PWMA_CC1E_Enable();
            break;

        case PWM2:
            PWMA_CC2E_Enable();
            break;

        case PWM3:
            PWMA_CC3E_Enable();
            break;

        case PWM4:
            PWMA_CC4E_Enable();
            break;

        case PWM5:
            PWMB_CC5E_Enable();
            break;

        case PWM6:
            PWMB_CC6E_Enable();
            break;

        case PWM7:
            PWMB_CC7E_Enable();
            break;

        case PWM8:
            PWMB_CC8E_Enable();
            break;

        default:
            return FAIL;
    }

    return SUCCESS;
}


/*
 * 函数功能：关闭指定 PWM 通道的比较输出。
 *
 * 参数：
 *     channel - PWM1 ~ PWM8。
 *
 * 返回值：
 *     SUCCESS - 通道输出已关闭。
 *     FAIL    - PWM 通道编号无效。
 *
 * 说明：
 *     关闭的是指定通道的比较输出，公共计数器仍可能继续运行，
 *     因此不会影响同一个 PWMA/PWMB 下的其他通道。
 */
u8 Pwm_Disable(u8 channel)
{
    switch (channel)
    {
        case PWM1:
            PWMA_CC1E_Disable();
            break;

        case PWM2:
            PWMA_CC2E_Disable();
            break;

        case PWM3:
            PWMA_CC3E_Disable();
            break;

        case PWM4:
            PWMA_CC4E_Disable();
            break;

        case PWM5:
            PWMB_CC5E_Disable();
            break;

        case PWM6:
            PWMB_CC6E_Disable();
            break;

        case PWM7:
            PWMB_CC7E_Disable();
            break;

        case PWM8:
            PWMB_CC8E_Disable();
            break;

        default:
            return FAIL;
    }

    return SUCCESS;
}
