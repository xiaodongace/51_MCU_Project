#include "App_Public.h"
#include "delay.h"


//=============================================开机动画
// 从中心向外扩散动画
void Animation_Expand(void)
{
    u8 i, j;
    u8 centerX = 64;
    u8 centerY = 32;
    u8 maxRadius;

    /* 计算最大半径（覆盖整个屏幕） */
    maxRadius = (centerX > centerY) ? centerX : centerY;
    if (128 - centerX > maxRadius) maxRadius = 128 - centerX;
    if (64 - centerY > maxRadius) maxRadius = 64 - centerY;

    /* 清空显存 */
    SPI_OLED_GClear();

    /* 从中心向外逐层点亮 */
    for (i = 0; i <= maxRadius; i++) {
        /* 绘制当前半径的圆 */
        for (j = 0; j <= i; j++) {
            /* 绘制四个象限的像素 */
            SPI_OLED_DrawPoint(centerX + j, centerY + i - j);
            SPI_OLED_DrawPoint(centerX - j, centerY + i - j);
            SPI_OLED_DrawPoint(centerX + j, centerY - i + j);
            SPI_OLED_DrawPoint(centerX - j, centerY - i + j);
        }

        /* 刷新屏幕 */
        SPI_OLED_Refresh();

        /* 延时控制动画速度 */
        /* Delay_ms(30); */
        delay_ms(15);
    }
}
// 从外向内收缩动画
void Animation_Contract(void)
{
    u8 i, j;
    u8 centerX = 64;
    u8 centerY = 32;
    u8 maxRadius;

    /* 计算最大半径 */
    maxRadius = (centerX > centerY) ? centerX : centerY;
    if (128 - centerX > maxRadius) maxRadius = 128 - centerX;
    if (64 - centerY > maxRadius) maxRadius = 64 - centerY;

    /* 先填充整个屏幕 */
    SPI_OLED_GFill();
    SPI_OLED_Refresh();

    /* 从外向内逐层清除 */
    for (i = maxRadius; i > 0; i--) {
        /* 清除当前半径的圆 */
        for (j = 0; j <= i; j++) {
            /* 清除四个象限的像素 */
            SPI_OLED_ClearPoint(centerX + j, centerY + i - j);
            SPI_OLED_ClearPoint(centerX - j, centerY + i - j);
            SPI_OLED_ClearPoint(centerX + j, centerY - i + j);
            SPI_OLED_ClearPoint(centerX - j, centerY - i + j);
        }

        /* 刷新屏幕 */
        SPI_OLED_Refresh();

        /* 延时控制动画速度 */
        /* Delay_ms(30); */
        delay_ms(15);
    }

    /* 最后清除中心点 */
    SPI_OLED_ClearPoint(centerX, centerY);
    SPI_OLED_GClear();
    SPI_OLED_Refresh();
}