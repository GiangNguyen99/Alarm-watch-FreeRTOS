/* Alarm Watch - Level 6 (CMSIS-RTOS2, STM32F411)
 * Keypad + DS3231 + LCD1602 + Alarm Event Flags.
 * Uses existing drivers without changing their APIs.
 */
#include "app_tasks.h"
#include "main.h"
#include "cmsis_os2.h"
#include "keypad.h"
#include "ds3231.h"
#include "lcd1602.h"
#include "uart_debug.h"
#include "alarm_flash.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c3;

typedef enum { EV_KEY, EV_RTC_OK, EV_RTC_BAD } EventType;
typedef struct {
    EventType type;
    union { char key; DS3231_DateTime time; } data;
} AppEvent;
typedef enum { SHOW_TIME, SET_TIME, SET_DATE, SET_ALARM } Screen;

typedef struct {
    DS3231_DateTime now;
    uint8_t rtcValid;
    Screen screen;
    char input[9];
    uint8_t inputLen;
    char notice[17];
    uint8_t alarmHour, alarmMinute, alarmEnabled, ringing;
} UiSnapshot;

#define ALARM_START (1UL << 0)
#define ALARM_STOP  (1UL << 1)

static osMessageQueueId_t eventQueue;
static osMutexId_t uiMutex;
static osMutexId_t rtcBusMutex;
static osEventFlagsId_t alarmFlags;
static osThreadId_t keypadHandle, rtcHandle, controllerHandle;
static osThreadId_t displayHandle, alarmHandle;
static UiSnapshot ui;
/* Loaded before osKernelStart; ControllerTask is the sole runtime owner. */
static uint8_t savedAlarmHour = 7U;
static uint8_t savedAlarmMinute = 0U;
static uint8_t savedAlarmEnabled = 0U;

static void KeypadTask(void *arg);
static void RtcTask(void *arg);
static void ControllerTask(void *arg);
static void DisplayTask(void *arg);
static void AlarmTask(void *arg);

static unsigned parseDigits(const char *text, unsigned offset, unsigned count)
{
    unsigned value = 0;
    for (unsigned i = 0; i < count; ++i)
        value = value * 10U + (unsigned)(text[offset + i] - '0');
    return value;
}

static uint32_t dateCode(const DS3231_DateTime *t)
{
    return (uint32_t)t->year * 10000UL + (uint32_t)t->month * 100UL + t->date;
}

/* Controller owns all UI changes; DisplayTask receives an atomic snapshot. */
static void publishUi(const UiSnapshot *state)
{
    if (osMutexAcquire(uiMutex, osWaitForever) == osOK) {
        ui = *state;
        osMutexRelease(uiMutex);
    }
}

static void setNotice(UiSnapshot *s, const char *message)
{
    snprintf(s->notice, sizeof s->notice, "%s", message);
}

static void clearEntry(UiSnapshot *s, Screen screen)
{
    s->screen = screen;
    s->inputLen = 0;
    s->input[0] = '\0';
    s->notice[0] = '\0';
}

static void stopAlarm(UiSnapshot *s)
{
    if (s->ringing) {
        s->ringing = 0;
        (void)osEventFlagsSet(alarmFlags, ALARM_STOP);
    }
}

/* Save only on explicit alarm configuration changes, never on RTC updates.
 * Flash erase/program is blocking and can briefly stall other tasks. */
static void saveAlarmConfig(const UiSnapshot *s)
{
    HAL_StatusTypeDef rc = AlarmFlash_Save(s->alarmHour,
                                           s->alarmMinute,
                                           s->alarmEnabled);
    if (rc == HAL_OK) {
        UART_Debug_Log(UART_LOG_INFO,
                       "[FLASH] Saved %02u:%02u %s\r\n",
                       (unsigned)s->alarmHour, (unsigned)s->alarmMinute,
                       s->alarmEnabled ? "ON" : "OFF");
    } else {
        UART_Debug_Log(UART_LOG_ERROR, "[FLASH] Save failed\r\n");
    }
}

static void handleKey(UiSnapshot *s, char key, DS3231_DateTime *draft)
{
    /* D always stops an ongoing alarm. */
    if (key == 'D') {
        stopAlarm(s);
        return;
    }
    if (s->ringing)
        return;

    if (s->screen == SHOW_TIME) {
        if (key == 'A') {
            *draft = s->rtcValid ? s->now : (DS3231_DateTime){
                .year = 2026, .month = 1, .date = 1, .hour = 0,
                .minute = 0, .second = 0, .day = 1
            };
            clearEntry(s, SET_TIME);
        } else if (key == 'B') {
            clearEntry(s, SET_ALARM);
        } else if (key == 'C') {
            s->alarmEnabled = !s->alarmEnabled;
            if (!s->alarmEnabled) stopAlarm(s);
            UART_Debug_Log(UART_LOG_INFO, "[ALARM] %s\r\n",
                           s->alarmEnabled ? "ON" : "OFF");
            saveAlarmConfig(s);
        }
        return;
    }

    /* Pressing A/B again cancels the corresponding editing mode. */
    if ((key == 'A' && (s->screen == SET_TIME || s->screen == SET_DATE)) ||
        (key == 'B' && s->screen == SET_ALARM)) {
        clearEntry(s, SHOW_TIME);
        return;
    }
    if (key == '*') {
        if (s->inputLen) s->input[--s->inputLen] = '\0';
        s->notice[0] = '\0';
        return;
    }
    if (key >= '0' && key <= '9') {
        unsigned required = s->screen == SET_DATE ? 8U :
                            s->screen == SET_TIME ? 6U : 4U;
        if (s->inputLen < required) {
            s->input[s->inputLen++] = key;
            s->input[s->inputLen] = '\0';
        }
        s->notice[0] = '\0';
        return;
    }
    if (key != '#') return;

    unsigned required = s->screen == SET_DATE ? 8U :
                        s->screen == SET_TIME ? 6U : 4U;
    if (s->inputLen != required) {
        setNotice(s, "NEED ALL DIGITS");
        return;
    }

    if (s->screen == SET_TIME) {
        unsigned h = parseDigits(s->input, 0, 2);
        unsigned m = parseDigits(s->input, 2, 2);
        unsigned sec = parseDigits(s->input, 4, 2);
        if (h > 23U || m > 59U || sec > 59U) {
            setNotice(s, "INVALID TIME");
            return;
        }
        draft->hour = (uint8_t)h;
        draft->minute = (uint8_t)m;
        draft->second = (uint8_t)sec;
        clearEntry(s, SET_DATE);
        return;
    }
    if (s->screen == SET_DATE) {
        draft->date = (uint8_t)parseDigits(s->input, 0, 2);
        draft->month = (uint8_t)parseDigits(s->input, 2, 2);
        draft->year = (uint16_t)parseDigits(s->input, 4, 4);
        if (!DS3231_Validate(draft)) {
            setNotice(s, "INVALID DATE");
            return;
        }
        /* RTC reads and writes must not overlap on I2C3. */
        HAL_StatusTypeDef rc = HAL_ERROR;
        if (osMutexAcquire(rtcBusMutex, osWaitForever) == osOK) {
            rc = DS3231_SetDateTime(draft);
            osMutexRelease(rtcBusMutex);
        }
        if (rc == HAL_OK) {
            clearEntry(s, SHOW_TIME);
            s->now = *draft;
            s->rtcValid = 1;
            UART_Debug_Log(UART_LOG_INFO, "[RTC] Time updated\r\n");
        } else {
            setNotice(s, "RTC WRITE ERROR");
            UART_Debug_Log(UART_LOG_ERROR, "[RTC] Write failed\r\n");
        }
        return;
    }
    if (s->screen == SET_ALARM) {
        unsigned h = parseDigits(s->input, 0, 2);
        unsigned m = parseDigits(s->input, 2, 2);
        if (h > 23U || m > 59U) {
            setNotice(s, "INVALID ALARM");
            return;
        }
        s->alarmHour = (uint8_t)h;
        s->alarmMinute = (uint8_t)m;
        clearEntry(s, SHOW_TIME);
        UART_Debug_Log(UART_LOG_INFO, "[ALARM] Set %02u:%02u\r\n", h, m);
        saveAlarmConfig(s);
    }
}

static void KeypadTask(void *arg)
{
    (void)arg;
    Keypad_Init();
    for (;;) {
        char key = Keypad_Read();
        if (key) {
            AppEvent e = {.type = EV_KEY};
            e.data.key = key;
            (void)osMessageQueuePut(eventQueue, &e, 0, 0);
        }
        osDelay(1); /* 1 ms if kernel tick frequency = 1 kHz */
    }
}

static void RtcTask(void *arg)
{
    (void)arg;
    uint32_t period = osKernelGetTickFreq() / 4U;
    if (period == 0U) period = 1U;
    uint32_t next = osKernelGetTickCount();
    for (;;) {
        AppEvent e = {.type = EV_RTC_BAD};
        DS3231_DateTime t = {0};
        uint8_t valid = 0;
        if (osMutexAcquire(rtcBusMutex, osWaitForever) == osOK) {
            HAL_StatusTypeDef rc = DS3231_TimeValid(&valid);
            if (rc == HAL_OK && valid) rc = DS3231_Read(&t);
            if (rc == HAL_OK && valid) {
                e.type = EV_RTC_OK;
                e.data.time = t;
            }
            osMutexRelease(rtcBusMutex);
        }
        (void)osMessageQueuePut(eventQueue, &e, 0, 0);
        next += period;
        if (osDelayUntil(next) != osOK)
            next = osKernelGetTickCount();
    }
}

static void ControllerTask(void *arg)
{
    (void)arg;
    UiSnapshot state = {0};
    DS3231_DateTime draft = {0};
    AppEvent event;
    uint32_t lastAlarmDate = 0;
    int lastSecond = -1;
    state.screen = SHOW_TIME;
    state.alarmHour = savedAlarmHour;
    state.alarmMinute = savedAlarmMinute;
    state.alarmEnabled = savedAlarmEnabled;
    publishUi(&state);

    for (;;) {
        if (osMessageQueueGet(eventQueue, &event, NULL, osWaitForever) != osOK)
            continue;
        if (event.type == EV_KEY) {
            handleKey(&state, event.data.key, &draft);
            UART_Debug_Log(UART_LOG_INFO, "[KEY] %c\r\n", event.data.key);
        } else if (event.type == EV_RTC_OK) {
            state.now = event.data.time;
            state.rtcValid = 1;
            if (lastSecond != (int)state.now.second) {
                lastSecond = state.now.second;
                UART_Debug_Log(UART_LOG_INFO,
                    "[RTC] %02u:%02u:%02u %02u/%02u/%04u\r\n",
                    state.now.hour, state.now.minute, state.now.second,
                    state.now.date, state.now.month, state.now.year);
            }
            uint32_t today = dateCode(&state.now);
            if (state.alarmEnabled && !state.ringing &&
                today != lastAlarmDate &&
                state.now.hour == state.alarmHour &&
                state.now.minute == state.alarmMinute) {
                state.ringing = 1;
                lastAlarmDate = today;
                (void)osEventFlagsClear(alarmFlags, ALARM_STOP);
                (void)osEventFlagsSet(alarmFlags, ALARM_START);
                UART_Debug_Log(UART_LOG_INFO, "[ALARM] RING\r\n");
            }
        } else if (event.type == EV_RTC_BAD) {
            state.rtcValid = 0;
            lastSecond = -1;
            stopAlarm(&state);
        }
        publishUi(&state);
    }
}

static void DisplayTask(void *arg)
{
    (void)arg;
    uint8_t ready = 0;
    char line1[17], line2[17], old1[17] = "", old2[17] = "";
    UiSnapshot snapshot;
    for (;;) {
        if (!ready) {
            if (LCD_Init(&hi2c1) != HAL_OK) {
                osDelay(2000);
                continue;
            }
            ready = 1;
            old1[0] = old2[0] = '\0';
        }
        if (osMutexAcquire(uiMutex, osWaitForever) == osOK) {
            snapshot = ui;
            osMutexRelease(uiMutex);
        } else {
            osDelay(100);
            continue;
        }
        if (snapshot.ringing) {
            snprintf(line1, sizeof line1, "*** ALARM ***");
            snprintf(line2, sizeof line2, "D:STOP %02u:%02u",
                     snapshot.alarmHour, snapshot.alarmMinute);
        } else if (snapshot.screen != SHOW_TIME) {
            const char *prompt = snapshot.screen == SET_TIME ? "TIME HHMMSS" :
                                 snapshot.screen == SET_DATE ? "DATE DDMMYYYY" :
                                                             "ALARM HHMM";
            snprintf(line1, sizeof line1, "%s", prompt);
            if (snapshot.notice[0])
                snprintf(line2, sizeof line2, "%s", snapshot.notice);
            else
                snprintf(line2, sizeof line2, "%s", snapshot.input);
        } else if (!snapshot.rtcValid) {
            snprintf(line1, sizeof line1, "RTC ERROR");
            snprintf(line2, sizeof line2, "A:SET TIME/DATE");
        } else {
            snprintf(line1, sizeof line1, "%02u:%02u:%02u A%02u:%02u",
                     (unsigned)(snapshot.now.hour % 100U),
                     (unsigned)(snapshot.now.minute % 100U),
                     (unsigned)(snapshot.now.second % 100U),
                     (unsigned)(snapshot.alarmHour % 100U),
                     (unsigned)(snapshot.alarmMinute % 100U));
            snprintf(line2, sizeof line2, "%02u/%02u/%04u %s",
                     snapshot.now.date, snapshot.now.month, snapshot.now.year,
                     snapshot.alarmEnabled ? "ON" : "OFF");
        }
        if (strcmp(line1, old1) != 0) {
            if (LCD_PrintLine(0, line1) != HAL_OK) ready = 0;
            else strcpy(old1, line1);
        }
        if (ready && strcmp(line2, old2) != 0) {
            if (LCD_PrintLine(1, line2) != HAL_OK) ready = 0;
            else strcpy(old2, line2);
        }
        osDelay(100);
    }
}

/* Only AlarmTask writes PD14: no GPIO ownership race with former LedTask. */
static void AlarmTask(void *arg)
{
    (void)arg;
    HAL_GPIO_WritePin(ALARM_LED_GPIO_Port, ALARM_LED_Pin, GPIO_PIN_RESET);
    for (;;) {
        uint32_t flags = osEventFlagsWait(alarmFlags, ALARM_START,
                                          osFlagsWaitAny, osWaitForever);
        if (flags & osFlagsError) continue;
        for (;;) {
            HAL_GPIO_TogglePin(ALARM_LED_GPIO_Port, ALARM_LED_Pin);
            flags = osEventFlagsWait(alarmFlags, ALARM_STOP,
                                     osFlagsWaitAny, 250);
            if (!(flags & osFlagsError)) break; /* STOP received */
            if (flags != osFlagsErrorTimeout) break;
        }
        HAL_GPIO_WritePin(ALARM_LED_GPIO_Port, ALARM_LED_Pin, GPIO_PIN_RESET);
    }
}

void AppTasks_Init(void)
{
    UART_Debug_Init(&huart2);
    UART_Debug_SetLevel(UART_LOG_INFO);
    DS3231_Init(&hi2c3);

    /* Read before tasks are created: no synchronization is needed here. */
    if (AlarmFlash_Load(&savedAlarmHour, &savedAlarmMinute,
                        &savedAlarmEnabled)) {
        UART_Debug_Log(UART_LOG_INFO,
                       "[FLASH] Loaded %02u:%02u %s\r\n",
                       (unsigned)savedAlarmHour, (unsigned)savedAlarmMinute,
                       savedAlarmEnabled ? "ON" : "OFF");
    } else {
        savedAlarmHour = 7U;
        savedAlarmMinute = 0U;
        savedAlarmEnabled = 0U;
        UART_Debug_Log(UART_LOG_INFO,
                       "[FLASH] No valid config, using 07:00 OFF\r\n");
    }

    eventQueue = osMessageQueueNew(24, sizeof(AppEvent), NULL);
    uiMutex = osMutexNew(NULL);
    rtcBusMutex = osMutexNew(NULL);
    alarmFlags = osEventFlagsNew(NULL);
    if (!eventQueue || !uiMutex || !rtcBusMutex || !alarmFlags)
        Error_Handler();

    const osThreadAttr_t controllerAttr = {
        .name = "ControllerTask", .priority = osPriorityAboveNormal,
        .stack_size = 2048
    };
    const osThreadAttr_t keypadAttr = {
        .name = "KeypadTask", .priority = osPriorityNormal,
        .stack_size = 1024
    };
    const osThreadAttr_t rtcAttr = {
        .name = "RtcTask", .priority = osPriorityNormal,
        .stack_size = 1024
    };
    const osThreadAttr_t displayAttr = {
        .name = "DisplayTask", .priority = osPriorityBelowNormal,
        .stack_size = 1536
    };
    const osThreadAttr_t alarmAttr = {
        .name = "AlarmTask", .priority = osPriorityLow,
        .stack_size = 512
    };
    controllerHandle = osThreadNew(ControllerTask, NULL, &controllerAttr);
    keypadHandle = osThreadNew(KeypadTask, NULL, &keypadAttr);
    rtcHandle = osThreadNew(RtcTask, NULL, &rtcAttr);
    displayHandle = osThreadNew(DisplayTask, NULL, &displayAttr);
    alarmHandle = osThreadNew(AlarmTask, NULL, &alarmAttr);
    if (!controllerHandle || !keypadHandle || !rtcHandle ||
        !displayHandle || !alarmHandle)
        Error_Handler();
}
