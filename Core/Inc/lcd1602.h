#ifndef INC_LCD1602_H_
#define INC_LCD1602_H_
#include "stm32f4xx_hal.h"
#define LCD_ADDR (0x27U << 1)
HAL_StatusTypeDef LCD_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef LCD_Clear(void);
HAL_StatusTypeDef LCD_SetCursor(uint8_t row, uint8_t col);
HAL_StatusTypeDef LCD_Print(const char *str);
/* Truncates/pads to exactly 16 characters. */
HAL_StatusTypeDef LCD_PrintLine(uint8_t row, const char *str);
#endif
