# Alarm Watch with FreeRTOS

A digital clock and daily alarm for the **STM32F411VET6**, built with STM32 HAL, FreeRTOS, and CMSIS-RTOS2. Time is provided by a DS3231 RTC, shown on a 16x2 I2C LCD, and configured through a 4x4 keypad. The alarm setting is retained in internal Flash after a reset or power loss.

## Features

- Displays time, date, alarm time, and alarm state on a 16x2 LCD
- Sets the RTC time/date and daily alarm from a 4x4 keypad
- Validates dates, leap years, and time ranges
- Flashes an LED while the alarm is ringing
- Stores the alarm time and enabled state in STM32 Flash
- Reports key, RTC, alarm, and Flash events over UART at 115200 baud
- Handles input, RTC polling, UI control, display, and alarm output in separate RTOS tasks

## Hardware

| Device | Interface | STM32 pins | Address / configuration |
| --- | --- | --- | --- |
| LCD1602 with I2C backpack | I2C1 | PB8 SCL, PB7 SDA | `0x27` |
| DS3231 RTC | I2C3 | PA8 SCL, PC9 SDA | `0x68` |
| 4x4 keypad rows | GPIO outputs | PB12, PB13, PB14, PB15 | R1-R4 |
| 4x4 keypad columns | GPIO inputs with pull-ups | PD8, PD9, PD10, PD11 | C1-C4 |
| Alarm LED | GPIO output | PD14 | Active high |
| Debug serial | USART2 | PA2 TX, PA3 RX | 115200, 8-N-1 |
| Programmer/debugger | SWD | PA13 SWDIO, PA14 SWCLK | ST-LINK compatible |

The I2C buses run at 100 kHz. Ensure SDA and SCL have suitable pull-up resistors; many RTC and LCD modules already include them.

## Keypad controls

| Key | Action |
| --- | --- |
| `A` | Enter time setup (`HHMMSS`), followed by date setup (`DDMMYYYY`); press again to cancel |
| `B` | Enter alarm setup (`HHMM`); press again to cancel |
| `C` | Enable or disable the alarm |
| `D` | Stop a ringing alarm |
| `0`-`9` | Enter values |
| `*` | Delete the last digit |
| `#` | Validate and confirm the current entry |

The alarm can trigger once per calendar day at the configured hour and minute. The default configuration is `07:00`, disabled, when no valid saved value exists.

## Software architecture

The application uses five CMSIS-RTOS2 tasks:

- **KeypadTask** scans and debounces the keypad.
- **RtcTask** reads the DS3231 four times per second.
- **ControllerTask** owns the application state and processes queued keypad/RTC events.
- **DisplayTask** renders state snapshots to the LCD.
- **AlarmTask** flashes the alarm LED using RTOS event flags.

A message queue transfers input and RTC events, mutexes protect the shared UI snapshot and RTC bus, and event flags start or stop the alarm. Alarm configuration is stored in Flash sector 7 at `0x08060000`; the linker limits application code to the first 384 KiB so this sector remains reserved.

## Build and flash

1. Install **STM32CubeIDE** with the **STM32CubeF4 v1.28.3** firmware package.
2. Import this directory as an existing STM32CubeIDE project.
3. Select the `Debug` configuration and build the project.
4. Connect the target through ST-LINK/SWD, then run or debug `Alarm-watch-FreeRTOS Debug.launch`.

Peripheral configuration is stored in [`Alarm-watch-FreeRTOS.ioc`](Alarm-watch-FreeRTOS.ioc). Application logic is mainly in [`Core/Src/app_tasks.c`](Core/Src/app_tasks.c), with device drivers under `Core/Src` and their public headers under `Core/Inc`.

## Documentation

- [System diagram](doc/DIGITAL%20CLOCK%20DIAGRAM.png)
- [RTOS task relationships](doc/TASK%20RELATIONSHIP.png)
- [DS3231 datasheet](doc/DS3231.PDF)
- [HD44780 LCD controller datasheet](doc/HD44780.pdf)
