#ifndef INC_DS3231_H_
#define INC_DS3231_H_
#include "stm32f4xx_hal.h"
#define DS3231_ADDR (0x68U << 1)

/* Calendar 2000..2099; day: Monday=1 .. Sunday=7. */
typedef struct {
	uint8_t second, minute, hour, day, date, month;
	uint16_t year;
} DS3231_DateTime;

typedef enum {
	DS3231_WRITE_OK = 0,
	DS3231_WRITE_INVALID_DATA,
	DS3231_WRITE_TIME_REGISTERS,
	DS3231_WRITE_STATUS_READ,
	DS3231_WRITE_STATUS_REGISTER
} DS3231_WriteStep;

void DS3231_Init(I2C_HandleTypeDef *hi2c);

HAL_StatusTypeDef DS3231_IsReady(void);

HAL_StatusTypeDef DS3231_Read(DS3231_DateTime *datetime);

HAL_StatusTypeDef DS3231_TimeValid(uint8_t *valid);

uint8_t DS3231_Validate(const DS3231_DateTime *datetime);

/* Calculates weekday; clears OSF after writing valid time. */
HAL_StatusTypeDef DS3231_SetDateTime(const DS3231_DateTime *datetime);

DS3231_WriteStep DS3231_GetLastWriteStep(void);

uint32_t DS3231_GetLastI2CError(void);

#endif
