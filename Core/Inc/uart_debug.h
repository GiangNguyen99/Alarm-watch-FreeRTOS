#ifndef INC_UART_DEBUG_H_
#define INC_UART_DEBUG_H_
#include "stm32f4xx_hal.h"
#ifndef UART_DEBUG_ENABLED
#define UART_DEBUG_ENABLED 1
#endif
typedef enum
{
    UART_LOG_OFF = 0,
    UART_LOG_ERROR,
    UART_LOG_INFO,
    UART_LOG_DEBUG
} UART_DebugLevel;
void UART_Debug_Init(UART_HandleTypeDef *huart);
void UART_Debug_SetLevel(UART_DebugLevel level);
HAL_StatusTypeDef UART_Debug_Print(const char *str);
#if defined(__GNUC__)
#define UART_FORMAT(a, b) __attribute__((format(printf, a, b)))
#else
#define UART_FORMAT(a, b)
#endif
HAL_StatusTypeDef UART_Debug_Printf(const char *format, ...) UART_FORMAT(1, 2);
/* Raw Print/Printf obey OFF; Log additionally filters by level. No ISR use. */
HAL_StatusTypeDef UART_Debug_Log(UART_DebugLevel level, const char *format, ...) UART_FORMAT(2, 3);
#endif
