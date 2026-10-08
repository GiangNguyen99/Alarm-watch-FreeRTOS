#ifndef INC_KEYPAD_H_
#define INC_KEYPAD_H_
#include "stm32f4xx_hal.h"
void Keypad_Init(void);
/* Call frequently from main; one press event or '\0'. */
char Keypad_Read(void);
#endif
