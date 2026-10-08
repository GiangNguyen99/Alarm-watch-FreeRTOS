#include "lcd1602.h"
#include <stddef.h>
#define LCD_RS 0x01U
#define LCD_EN 0x04U
#define LCD_BACKLIGHT 0x08U
static I2C_HandleTypeDef *lcd_bus;
static uint8_t cursor_col;
static HAL_StatusTypeDef nibble(uint8_t data, uint8_t mode)
{
	uint8_t v = (data & 0xF0U) | LCD_BACKLIGHT | mode;
	uint8_t tx[] = {v, v | LCD_EN, v}; /* Set data before raising enable. */
	if (!lcd_bus)
		return HAL_ERROR;
	return HAL_I2C_Master_Transmit(lcd_bus, LCD_ADDR, tx, sizeof tx, 10);
}
static HAL_StatusTypeDef byte(uint8_t data, uint8_t mode)
{
	HAL_StatusTypeDef rc = nibble(data, mode);
	return rc == HAL_OK ? nibble((uint8_t)(data << 4), mode) : rc;
}
static HAL_StatusTypeDef command(uint8_t cmd)
{
	HAL_StatusTypeDef rc = byte(cmd, 0);
	if (rc == HAL_OK && (cmd == 1U || cmd == 2U))
		HAL_Delay(2);
	return rc;
}
HAL_StatusTypeDef LCD_Clear(void)
{
	HAL_StatusTypeDef rc = command(1);
	if (rc == HAL_OK)
		cursor_col = 0;
	return rc;
}
HAL_StatusTypeDef LCD_Init(I2C_HandleTypeDef *hi2c)
{
	lcd_bus = hi2c;
	cursor_col = 0;
	if (!lcd_bus)
		return HAL_ERROR;
	HAL_StatusTypeDef rc = HAL_I2C_IsDeviceReady(lcd_bus, LCD_ADDR, 1, 10);
	if (rc != HAL_OK)
		return rc;
	HAL_Delay(50);
	const uint8_t seq[] = {0x30, 0x30, 0x30, 0x20};
	for (unsigned i = 0; i < 4; ++i)
	{
		rc = nibble(seq[i], 0);
		if (rc != HAL_OK)
			return rc;
		HAL_Delay(i == 0 ? 5 : 1);
	}
	const uint8_t cmds[] = {0x28, 0x0C, 0x06, 0x01};
	for (unsigned i = 0; i < 4; ++i)
	{
		rc = command(cmds[i]);
		if (rc != HAL_OK)
			return rc;
	}
	return HAL_OK;
}
HAL_StatusTypeDef LCD_SetCursor(uint8_t row, uint8_t col)
{
	if (row >= 2U || col >= 16U)
		return HAL_ERROR;
	HAL_StatusTypeDef rc = command(0x80U | (row ? 0x40U + col : col));
	if (rc == HAL_OK)
		cursor_col = col;
	return rc;
}
HAL_StatusTypeDef LCD_Print(const char *str)
{
	if (!lcd_bus || !str)
		return HAL_ERROR;
	while (*str && cursor_col < 16U)
	{
		HAL_StatusTypeDef rc = byte((uint8_t)*str++, LCD_RS);
		if (rc != HAL_OK)
			return rc;
		++cursor_col;
	}
	return HAL_OK;
}
HAL_StatusTypeDef LCD_PrintLine(uint8_t row, const char *str)
{
	char line[17];
	unsigned i = 0;
	if (!str)
		return HAL_ERROR;
	while (i < 16U && str[i])
	{
		line[i] = str[i];
		++i;
	}
	while (i < 16U)
		line[i++] = ' ';
	line[16] = '\0';
	HAL_StatusTypeDef rc = LCD_SetCursor(row, 0);
	return rc == HAL_OK ? LCD_Print(line) : rc;
}
