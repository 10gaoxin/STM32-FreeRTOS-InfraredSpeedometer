/**
  ******************************************************************************
  * @file    oled.h
  * @brief   0.96寸 OLED (SSD1306) I2C驱动头文件
  ******************************************************************************
  */

#ifndef __OLED_H
#define __OLED_H

#include "main.h"
#include "i2c.h"

/* OLED I2C地址 (7位地址左移1位，自动检测) */
#define OLED_I2C_ADDR       0x78    /* 0x3C << 1 (默认) */
#define OLED_I2C_ADDR_ALT   0x7A    /* 0x3D << 1 (备用，SA0引脚接高) */

/* OLED尺寸 */
#define OLED_WIDTH          128
#define OLED_HEIGHT         64

/* 字体大小定义 */
#define OLED_FONT_6X8       1
#define OLED_FONT_8X16      2

/* 函数声明 */

/* 初始化与基本操作 */
void OLED_Init(void);
uint8_t OLED_IsReady(void);
void OLED_Clear(void);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Refresh(void);

/* 设置坐标 */
void OLED_SetCursor(uint8_t x, uint8_t y);

/* 绘图函数 */
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t mode);
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);
void OLED_DrawRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);
void OLED_Fill(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t mode);

/* 显示字符和字符串 */
void OLED_ShowChar(uint8_t x, uint8_t y, char chr, uint8_t size);
void OLED_ShowString(uint8_t x, uint8_t y, const char *str, uint8_t size);
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size);
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t intLen, uint8_t decLen, uint8_t size);

/* 显示中文 (16x16) */
void OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t index);

#endif /* __OLED_H */
