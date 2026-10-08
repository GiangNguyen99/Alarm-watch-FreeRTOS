/*
 * alarm_flash.h
 *
 *  Created on: 6 thg 10, 2026
 *      Author: Admin
 */

#ifndef INC_ALARM_FLASH_H_
#define INC_ALARM_FLASH_H_

#include "stm32f4xx_hal.h"

HAL_StatusTypeDef AlarmFlash_Save(uint8_t hour,
                                  uint8_t minute,
                                  uint8_t enabled);

uint8_t AlarmFlash_Load(uint8_t *hour,
                        uint8_t *minute,
                        uint8_t *enabled);


#endif /* INC_ALARM_FLASH_H_ */
