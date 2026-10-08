#include "keypad.h"
#include "main.h"
static GPIO_TypeDef *const rows[] = {KP_R1_GPIO_Port, KP_R2_GPIO_Port,
									 KP_R3_GPIO_Port, KP_R4_GPIO_Port};
static GPIO_TypeDef *const cols[] = {KP_C1_GPIO_Port, KP_C2_GPIO_Port,
									 KP_C3_GPIO_Port, KP_C4_GPIO_Port};
static const uint16_t rp[] = {KP_R1_Pin, KP_R2_Pin, KP_R3_Pin, KP_R4_Pin};
static const uint16_t cp[] = {KP_C1_Pin, KP_C2_Pin, KP_C3_Pin, KP_C4_Pin};
static const char keys[] = "123A456B789C*0#D";
static uint8_t row, locked;
static uint16_t frame, candidate, stable;
static uint32_t row_tick, change_tick;
void Keypad_Init(void)
{
	for (unsigned i = 0; i < 4; ++i)
		HAL_GPIO_WritePin(rows[i], rp[i], GPIO_PIN_SET);
	row = 0;
	frame = 0;
	candidate = 0;
	stable = 0;
	locked = 0;
	row_tick = change_tick = HAL_GetTick();
	HAL_GPIO_WritePin(rows[0], rp[0], GPIO_PIN_RESET);
}
char Keypad_Read(void)
{
	uint32_t now = HAL_GetTick();
	if ((uint32_t)(now - row_tick) < 1U)
		return '\0';
	row_tick = now;
	for (unsigned c = 0; c < 4; ++c)
		if (HAL_GPIO_ReadPin(cols[c], cp[c]) == GPIO_PIN_RESET)
			frame |= 1U << (row * 4U + c);
	HAL_GPIO_WritePin(rows[row], rp[row], GPIO_PIN_SET);
	row = (row + 1U) % 4U;
	HAL_GPIO_WritePin(rows[row], rp[row], GPIO_PIN_RESET);
	if (row != 0)
		return '\0';
	uint16_t sample = frame;
	frame = 0;
	if (sample && (sample & (sample - 1U)))
		locked = 1;
	if (sample != candidate)
	{
		candidate = sample;
		change_tick = now;
	}
	if ((uint32_t)(now - change_tick) < 20U)
		return '\0';
	if (!sample)
		locked = 0;
	if (sample == stable)
		return '\0';
	stable = sample;
	if (!stable)
	{
		locked = 0;
		return '\0';
	}
	if (locked)
		return '\0';
	locked = 1;
	for (unsigned i = 0; i < 16; ++i)
		if (stable == (1U << i))
			return keys[i];
	return '\0';
}
