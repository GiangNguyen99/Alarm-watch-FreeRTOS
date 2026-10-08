#include "ds3231.h"
#include <stddef.h>
static I2C_HandleTypeDef *rtc_bus;
#define RTC_TIMEOUT 100U
static DS3231_WriteStep last_write_step;
static uint32_t last_i2c_error;
static uint8_t dec(uint8_t v)
{
	return (v >> 4) * 10U + (v & 15U);
}
static uint8_t bcd(uint8_t v)
{
	return ((v / 10U) << 4) | (v % 10U);
}
static uint8_t bcd_valid(uint8_t v)
{
	return (v & 15U) < 10U && (v >> 4) < 10U;
}
static uint8_t month_days(uint16_t y, uint8_t m)
{
	static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30,
								   31};
	return days[m - 1U] + (m == 2U && y % 4U == 0U);
}
uint8_t DS3231_Validate(const DS3231_DateTime *t)
{
	return t && t->year >= 2000U && t->year <= 2099U && t->month >= 1U && t->month <= 12U && t->date >= 1U && t->date <= month_days(t->year, t->month) && t->hour < 24U && t->minute < 60U && t->second < 60U;
}
void DS3231_Init(I2C_HandleTypeDef *hi2c)
{
	rtc_bus = hi2c;
}
HAL_StatusTypeDef DS3231_IsReady(void)
{
	return rtc_bus ? HAL_I2C_IsDeviceReady(rtc_bus, DS3231_ADDR, 1, RTC_TIMEOUT) : HAL_ERROR;
}
HAL_StatusTypeDef DS3231_TimeValid(uint8_t *valid)
{
	uint8_t status;
	if (!rtc_bus || !valid)
		return HAL_ERROR;
	*valid = 0;
	HAL_StatusTypeDef rc = HAL_I2C_Mem_Read(rtc_bus, DS3231_ADDR, 0x0F,
											I2C_MEMADD_SIZE_8BIT, &status, 1, RTC_TIMEOUT);
	if (rc == HAL_OK)
		*valid = !(status & 0x80U);
	return rc;
}
HAL_StatusTypeDef DS3231_Read(DS3231_DateTime *t)
{
	uint8_t d[7];
	DS3231_DateTime next;
	if (!rtc_bus || !t)
		return HAL_ERROR;
	HAL_StatusTypeDef rc = HAL_I2C_Mem_Read(rtc_bus, DS3231_ADDR, 0,
											I2C_MEMADD_SIZE_8BIT, d, sizeof d, RTC_TIMEOUT);
	if (rc != HAL_OK)
		return rc;
	uint8_t h = d[2] & ((d[2] & 0x40U) ? 0x1FU : 0x3FU);
	if ((d[0] & 0x80U) || (d[1] & 0x80U) || (d[2] & 0x80U) || (d[3] & 0xF8U) || (d[4] & 0xC0U) || (d[5] & 0xE0U) || !bcd_valid(d[0]) || !bcd_valid(d[1]) || !bcd_valid(h) || !bcd_valid(d[4]) || !bcd_valid(d[5]) || !bcd_valid(d[6]))
		return HAL_ERROR;
	next.second = dec(d[0]);
	next.minute = dec(d[1]);
	next.hour = dec(h);
	if (d[2] & 0x40U)
	{
		if (next.hour < 1U || next.hour > 12U)
			return HAL_ERROR;
		next.hour = next.hour % 12U + ((d[2] & 0x20U) ? 12U : 0U);
	}
	next.day = d[3];
	next.date = dec(d[4]);
	next.month = dec(d[5]);
	next.year = 2000U + dec(d[6]);
	if (!DS3231_Validate(&next) || next.day < 1U || next.day > 7U)
		return HAL_ERROR;
	*t = next;
	return HAL_OK;
}
HAL_StatusTypeDef DS3231_SetDateTime(const DS3231_DateTime *t)
{
	uint8_t d[7], status;
	uint32_t days = 0;
	last_write_step = DS3231_WRITE_INVALID_DATA;
	last_i2c_error = HAL_I2C_ERROR_NONE;
	if (!rtc_bus || !DS3231_Validate(t))
		return HAL_ERROR;
	for (uint16_t y = 2000; y < t->year; ++y)
		days += 365U + (y % 4U == 0U);
	for (uint8_t m = 1; m < t->month; ++m)
		days += month_days(t->year, m);
	days += t->date - 1U;
	d[0] = bcd(t->second);
	d[1] = bcd(t->minute);
	d[2] = bcd(t->hour);
	d[3] = (days + 5U) % 7U + 1U; /* 2000-01-01: Saturday. */
	d[4] = bcd(t->date);
	d[5] = bcd(t->month);
	d[6] = bcd(t->year - 2000U);
	HAL_StatusTypeDef rc = HAL_I2C_Mem_Write(rtc_bus, DS3231_ADDR, 0,
											 I2C_MEMADD_SIZE_8BIT, d, sizeof d, RTC_TIMEOUT);
	if (rc != HAL_OK) {
		last_write_step = DS3231_WRITE_TIME_REGISTERS;
		last_i2c_error = HAL_I2C_GetError(rtc_bus);
		return rc;
	}
	rc = HAL_I2C_Mem_Read(rtc_bus, DS3231_ADDR, 0x0F,
						  I2C_MEMADD_SIZE_8BIT, &status, 1, RTC_TIMEOUT);
	if (rc != HAL_OK) {
		last_write_step = DS3231_WRITE_STATUS_READ;
		last_i2c_error = HAL_I2C_GetError(rtc_bus);
		return rc;
	}
	status = (status & 0x7FU) | 0x03U; /* Leave alarm flags unchanged. */
	rc = HAL_I2C_Mem_Write(rtc_bus, DS3231_ADDR, 0x0F,
							 I2C_MEMADD_SIZE_8BIT, &status, 1, RTC_TIMEOUT);
	if (rc != HAL_OK) {
		last_write_step = DS3231_WRITE_STATUS_REGISTER;
		last_i2c_error = HAL_I2C_GetError(rtc_bus);
		return rc;
	}
	last_write_step = DS3231_WRITE_OK;
	return HAL_OK;
}
DS3231_WriteStep DS3231_GetLastWriteStep(void)
{
	return last_write_step;
}
uint32_t DS3231_GetLastI2CError(void)
{
	return last_i2c_error;
}
