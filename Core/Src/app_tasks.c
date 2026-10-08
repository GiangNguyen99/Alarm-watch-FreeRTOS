
/*
 * app_tasks.c
 * Alarm Watch - FreeRTOS Level 2
 * CMSIS-RTOS V2
 */

#include "app_tasks.h"
#include "main.h"
#include "cmsis_os2.h"

#include "keypad.h"
#include "uart_debug.h"

/* External peripheral handles */
extern UART_HandleTypeDef huart2;

/* Task handles */
static osThreadId_t keypadHandle = NULL;
static osThreadId_t controllerHandle = NULL;
static osThreadId_t ledHandle = NULL;

/* Message Queue */
static osMessageQueueId_t keypadQueueHandle = NULL;

/* Task prototypes */
static void KeypadTask(void *argument);
static void ControllerTask(void *argument);
static void LedTask(void *argument);


/* =========================================
 * Keypad Task
 * Read keypad and send key to Queue
 * ========================================= */
static void KeypadTask(void *argument)
{
    (void)argument;

    Keypad_Init();

    for (;;)
    {
        char key = Keypad_Read();

        if (key != '\0')
        {
            osStatus_t status = osMessageQueuePut(
                keypadQueueHandle,
                &key,
                0,
                0
            );

            if (status != osOK)
            {
                // Queue full or error.
                // Do not block keypad scanning.
            }
        }

        osDelay(1);
    }
}


/* =========================================
 * Controller Task
 * Receive key from Queue
 * ========================================= */
static void ControllerTask(void *argument)
{
    (void)argument;

    char key;

    for (;;)
    {
        osStatus_t status = osMessageQueueGet(
            keypadQueueHandle,
            &key,
            NULL,
            osWaitForever
        );

        if (status == osOK)
        {
            UART_Debug_Log(
                UART_LOG_INFO,
                "[CONTROLLER] Key: %c\r\n",
                key
            );
        }
    }
}


/* =========================================
 * LED Task
 * Heartbeat LED PD14
 * ========================================= */
static void LedTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        HAL_GPIO_TogglePin(
            ALARM_LED_GPIO_Port,
            ALARM_LED_Pin
        );

        osDelay(500);
    }
}


/* =========================================
 * Initialize FreeRTOS objects
 * Call after osKernelInitialize()
 * and before osKernelStart()
 * ========================================= */
void AppTasks_Init(void)
{
    /* Initialize UART logger */
    UART_Debug_Init(&huart2);
    UART_Debug_SetLevel(UART_LOG_INFO);

    /* Create Message Queue */
    keypadQueueHandle = osMessageQueueNew(
        16,
        sizeof(char),
        NULL
    );

    if (keypadQueueHandle == NULL)
    {
        Error_Handler();
    }

    /* Task attributes */
    const osThreadAttr_t keypadAttr = {
        .name = "KeypadTask",
        .priority = osPriorityNormal,
        .stack_size = 1024
    };

    const osThreadAttr_t controllerAttr = {
        .name = "ControllerTask",
        .priority = osPriorityAboveNormal,
        .stack_size = 1024
    };

    const osThreadAttr_t ledAttr = {
        .name = "LedTask",
        .priority = osPriorityLow,
        .stack_size = 512
    };

    /* Create Tasks */
    keypadHandle = osThreadNew(
        KeypadTask,
        NULL,
        &keypadAttr
    );

    controllerHandle = osThreadNew(
        ControllerTask,
        NULL,
        &controllerAttr
    );

    ledHandle = osThreadNew(
        LedTask,
        NULL,
        &ledAttr
    );

    /* Check Task creation */
    if (keypadHandle == NULL ||
        controllerHandle == NULL ||
        ledHandle == NULL)
    {
        Error_Handler();
    }
}
