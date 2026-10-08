
#include "app_tasks.h"
#include "main.h"
#include "keypad.h"
#include "uart_debug.h"

extern UART_HandleTypeDef huart2;

static osThreadId_t keypadHandle;
static osThreadId_t ledHandle;

static void KeypadTask(void *argument)
{
    Keypad_Init();

    for (;;)
    {
        char key = Keypad_Read();

        if (key != '\0')
        {
            UART_Debug_Log(
                UART_LOG_INFO,
                "[KEYPAD] Key: %c\r\n",
                key
            );
        }

        osDelay(1);
    }
}

static void LedTask(void *argument)
{
    for (;;)
    {
        HAL_GPIO_TogglePin(
            ALARM_LED_GPIO_Port,
            ALARM_LED_Pin
        );

        osDelay(500);
    }
}

void AppTasks_Init(void)
{
    UART_Debug_Init(&huart2);

    const osThreadAttr_t keypadAttr = {
        .name = "KeypadTask",
        .priority = osPriorityNormal,
        .stack_size = 1024
    };

    const osThreadAttr_t ledAttr = {
        .name = "LedTask",
        .priority = osPriorityLow,
        .stack_size = 512
    };

    keypadHandle = osThreadNew(
        KeypadTask,
        NULL,
        &keypadAttr
    );

    ledHandle = osThreadNew(
        LedTask,
        NULL,
        &ledAttr
    );

    if (keypadHandle == NULL ||
        ledHandle == NULL)
    {
        Error_Handler();
    }
}
