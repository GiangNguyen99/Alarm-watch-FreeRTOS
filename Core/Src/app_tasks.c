
/*
 * Alarm Watch - FreeRTOS Level 3
 * CMSIS-RTOS V2
 *
 * Concepts:
 * - Multiple producers, single consumer
 * - Message Queue with struct
 * - Periodic task using osDelayUntil
 * - Task priorities
 */

#include "app_tasks.h"
#include "main.h"
#include "cmsis_os2.h"

#include "keypad.h"
#include "ds3231.h"
#include "uart_debug.h"

extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef hi2c3;

/* ---------- Event definitions ---------- */

typedef enum {
    EVENT_KEY,
    EVENT_RTC,
    EVENT_RTC_ERROR
} AppEventType;

typedef struct {
    AppEventType type;

    union {
        char key;
        DS3231_DateTime datetime;
    } data;

} AppEvent;

/* ---------- RTOS handles ---------- */

static osMessageQueueId_t eventQueue = NULL;

static osThreadId_t keypadHandle = NULL;
static osThreadId_t rtcHandle = NULL;
static osThreadId_t controllerHandle = NULL;
static osThreadId_t ledHandle = NULL;

/* ---------- Task prototypes ---------- */

static void KeypadTask(void *argument);
static void RtcTask(void *argument);
static void ControllerTask(void *argument);
static void LedTask(void *argument);

/* ---------- Keypad Task ---------- */

static void KeypadTask(void *argument)
{
    (void)argument;

    Keypad_Init();

    for (;;)
    {
        char key = Keypad_Read();

        if (key != '\0')
        {
            AppEvent event = {0};

            event.type = EVENT_KEY;
            event.data.key = key;

            if (osMessageQueuePut(
                    eventQueue,
                    &event,
                    0,
                    0) != osOK)
            {
                // TODO: Count dropped events
            }
        }

        osDelay(1);
    }
}

/* ---------- RTC Task ---------- */

static void RtcTask(void *argument)
{
    (void)argument;

    uint32_t period = osKernelGetTickFreq() / 4U;
    uint32_t nextWake = osKernelGetTickCount();

    if (period == 0U)
        period = 1U;

    for (;;)
    {
        DS3231_DateTime datetime = {0};
        uint8_t valid = 0;

        AppEvent event = {0};

        HAL_StatusTypeDef status =
            DS3231_TimeValid(&valid);

        if (status == HAL_OK && valid)
        {
            status = DS3231_Read(&datetime);
        }

        if (status == HAL_OK && valid)
        {
            event.type = EVENT_RTC;
            event.data.datetime = datetime;
        }
        else
        {
            event.type = EVENT_RTC_ERROR;
        }

        if (osMessageQueuePut(
                eventQueue,
                &event,
                0,
                0) != osOK)
        {
            // TODO: Count dropped events
        }

        nextWake += period;
        (void)osDelayUntil(nextWake);
    }
}

/* ---------- Controller Task ---------- */

static void ControllerTask(void *argument)
{
    (void)argument;

    AppEvent event;
    int lastSecond = -1;
    uint8_t rtcErrorReported = 0;

    for (;;)
    {
        osStatus_t status = osMessageQueueGet(
            eventQueue,
            &event,
            NULL,
            osWaitForever
        );

        if (status != osOK)
            continue;

        switch (event.type)
        {
            case EVENT_KEY:
            {
                UART_Debug_Log(
                    UART_LOG_INFO,
                    "[KEY] %c\r\n",
                    event.data.key
                );
                break;
            }

            case EVENT_RTC:
            {
                DS3231_DateTime *t =
                    &event.data.datetime;

                rtcErrorReported = 0;

                if (lastSecond != (int)t->second)
                {
                    lastSecond = t->second;

                    UART_Debug_Log(
                        UART_LOG_INFO,
                        "[RTC] %02u:%02u:%02u "
                        "%02u/%02u/%04u\r\n",
                        (unsigned)t->hour,
                        (unsigned)t->minute,
                        (unsigned)t->second,
                        (unsigned)t->date,
                        (unsigned)t->month,
                        (unsigned)t->year
                    );
                }
                break;
            }

            case EVENT_RTC_ERROR:
            {
                lastSecond = -1;

                if (!rtcErrorReported)
                {
                    UART_Debug_Log(
                        UART_LOG_ERROR,
                        "[RTC] Read error or "
                        "invalid time\r\n"
                    );

                    rtcErrorReported = 1;
                }
                break;
            }

            default:
                break;
        }
    }
}

/* ---------- LED Task ---------- */

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

/* ---------- RTOS initialization ---------- */

void AppTasks_Init(void)
{
    UART_Debug_Init(&huart2);
    UART_Debug_SetLevel(UART_LOG_INFO);

    DS3231_Init(&hi2c3);

    eventQueue = osMessageQueueNew(
        16,
        sizeof(AppEvent),
        NULL
    );

    if (eventQueue == NULL)
        Error_Handler();

    const osThreadAttr_t keypadAttr = {
        .name = "KeypadTask",
        .priority = osPriorityNormal,
        .stack_size = 1024
    };

    const osThreadAttr_t rtcAttr = {
        .name = "RtcTask",
        .priority = osPriorityNormal,
        .stack_size = 1024
    };

    const osThreadAttr_t controllerAttr = {
        .name = "ControllerTask",
        .priority = osPriorityAboveNormal,
        .stack_size = 1536
    };

    const osThreadAttr_t ledAttr = {
        .name = "LedTask",
        .priority = osPriorityLow,
        .stack_size = 512
    };

    controllerHandle = osThreadNew(
        ControllerTask, NULL, &controllerAttr
    );

    keypadHandle = osThreadNew(
        KeypadTask, NULL, &keypadAttr
    );

    rtcHandle = osThreadNew(
        RtcTask, NULL, &rtcAttr
    );

    ledHandle = osThreadNew(
        LedTask, NULL, &ledAttr
    );

    if (controllerHandle == NULL ||
        keypadHandle == NULL ||
        rtcHandle == NULL ||
        ledHandle == NULL)
    {
        Error_Handler();
    }
}
