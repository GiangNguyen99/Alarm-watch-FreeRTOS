#include "uart_debug.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
static UART_HandleTypeDef *debug_uart;
static UART_DebugLevel threshold = UART_LOG_INFO;
void UART_Debug_Init(UART_HandleTypeDef *huart) {
	debug_uart = huart;
}
void UART_Debug_SetLevel(UART_DebugLevel level) {
	threshold = level;
}
HAL_StatusTypeDef UART_Debug_Print(const char *str) {
	if (!str)
		return HAL_ERROR;
	if (!UART_DEBUG_ENABLED || threshold == UART_LOG_OFF)
		return HAL_OK;
	if (!debug_uart)
		return HAL_ERROR;
	size_t len = strlen(str);
	if (len > UINT16_MAX)
		return HAL_ERROR;
	if (!len)
		return HAL_OK;
	return HAL_UART_Transmit(debug_uart, (const uint8_t*) str, (uint16_t) len,
			20);
}
static HAL_StatusTypeDef print_args(const char *fmt, va_list args) {
	char buf[128];
	if (!fmt)
		return HAL_ERROR;
	if (!UART_DEBUG_ENABLED || threshold == UART_LOG_OFF)
		return HAL_OK;
	int n = vsnprintf(buf, sizeof buf, fmt, args);
	if (n < 0 || (size_t) n >= sizeof buf)
		return HAL_ERROR;
	return UART_Debug_Print(buf);
}
HAL_StatusTypeDef UART_Debug_Printf(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	HAL_StatusTypeDef rc = print_args(fmt, args);
	va_end(args);
	return rc;
}
HAL_StatusTypeDef UART_Debug_Log(UART_DebugLevel level, const char *fmt, ...) {
	if (level == UART_LOG_OFF || level > threshold)
		return HAL_OK;
	va_list args;
	va_start(args, fmt);
	HAL_StatusTypeDef rc = print_args(fmt, args);
	va_end(args);
	return rc;
}
