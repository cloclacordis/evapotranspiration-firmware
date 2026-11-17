/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Tim Alexeenko
 * E-mail: <cloclacordis@proton.me> */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/atomic.h>

#include "service-console.h"
#include "rtc-service.h"

#include "03-validation/033-status/status.h"
#include "02-providers/023-time/calendar-time.h"
#include "02-providers/023-time/time-provider.h"
#include "02-providers/021-date-provider/date-provider.h"

#define UART_RX_BUFFER_SIZE 128U
#define UART_RX_CHUNK_SIZE  16U

#define LINE_CAPACITY       48U
#define ISO_UTC_LENGTH      20U
#define CMD_SET_PREFIX      "SET-UTC "

enum LineError {
    LINE_ERROR_NONE = 0,
    LINE_ERROR_TOO_LONG,
    LINE_ERROR_RX_OVERRUN
};

/* The board selects USART2 as `zephyr,console` */
static const struct device *const s_uart_dev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* Single ISR producer, single thread consumer */
static struct   ring_buf s_rx_ring;
static uint8_t  s_rx_storage[UART_RX_BUFFER_SIZE];
static atomic_t s_rx_overrun;

static char     s_line[LINE_CAPACITY];
static size_t   s_length;
static enum     LineError s_line_error;
static bool     s_uart_ready;

/* *** UART RX interrupt *** */

static void UartRxCallback(const struct device *dev, void *user_data) {
    (void)user_data;

    /* Required before checking RX interrupt status */
    uart_irq_update(dev);

    if (uart_irq_rx_ready(dev) <= 0) {
        return;
    }

    for (;;) {
        uint8_t data[UART_RX_CHUNK_SIZE];
        const int received = uart_fifo_read(dev, data, (int)sizeof(data));

        if (received <= 0) {
            break;
        }

        const uint32_t stored = ring_buf_put(&s_rx_ring, data, (uint32_t)received);

        if (stored != (uint32_t)received) {
            /* The parser will reject the affected line, not execute it */
            atomic_set(&s_rx_overrun, 1);
        }

        /* Read again if the UART FIFO returned a full chunk */
        if ((size_t)received < sizeof(data)) {
            break;
        }
    }
}

/* *** UTC command parsing *** */

static bool ParseDigits(const char *text, const size_t count, uint32_t *out_value) {
    uint32_t value = 0U;

    for (size_t i = 0U; i < count; ++i) {
        if ((text[i] < '0') || (text[i] > '9')) {
            return false;
        }

        value = (value * 10U) + (uint32_t)(text[i] - '0');
    }

    *out_value = value;
    return true;
}

/* Accept exactly YYYY-MM-DDTHH:MM:SSZ; Z means UTC */
static Status ParseUtcTimestamp(const char *text, uint32_t *out_timestamp) {
    if ((text == NULL) || (out_timestamp == NULL) ||
        (strlen(text) != ISO_UTC_LENGTH)) {
        return STATUS_INVALID_VALUE;
    }

    uint32_t year   = 0U;
    uint32_t month  = 0U;
    uint32_t day    = 0U;
    uint32_t hour   = 0U;
    uint32_t minute = 0U;
    uint32_t second = 0U;

    if ((text[4]  != '-') || (text[7]  != '-') || (text[10] != 'T') ||
        (text[13] != ':') || (text[16] != ':') || (text[19] != 'Z') ||
        !ParseDigits(&text[0],  4U, &year)   ||
        !ParseDigits(&text[5],  2U, &month)  ||
        !ParseDigits(&text[8],  2U, &day)    ||
        !ParseDigits(&text[11], 2U, &hour)   ||
        !ParseDigits(&text[14], 2U, &minute) ||
        !ParseDigits(&text[17], 2U, &second)) {
        return STATUS_INVALID_VALUE;
    }

    const CalendarTime utc = {
        (uint16_t)year,
        (uint8_t)month,
        (uint8_t)day,
        (uint8_t)hour,
        (uint8_t)minute,
        (uint8_t)second
    };

    return CalendarTime_ToEpoch(&utc, out_timestamp);
}

/* *** Console output *** */

void Console_PrintCurrentTime(void) {
    const RTCState state = RTC_GetState();

    (void)printf("\nRTC state: %s\n", RTCState_ToString(state));

    uint32_t timestamp = 0U;
    const Status time_status = TimeProvider_ReadTimestamp(&timestamp);

    if (time_status != STATUS_OK) {
        (void)printf("UTC time unavailable: %s\n", Status_ToString(time_status));
        return;
    }

    CalendarTime utc;
    const Status calendar_status = CalendarTime_FromEpoch(timestamp, 0, &utc);

    if (calendar_status != STATUS_OK) {
        (void)printf("UTC conversion failed: %s\n", Status_ToString(calendar_status));
        return;
    }

    (void)printf("UTC: %04u-%02u-%02uT%02u:%02u:%02uZ\n", (unsigned)utc.year,
                 (unsigned)utc.month,  (unsigned)utc.day, (unsigned)utc.hour,
                 (unsigned)utc.minute, (unsigned)utc.second);

    DateData station_date;
    const Status date_status = DateProvider_Read(&station_date);

    if (date_status == STATUS_OK) {
        (void)printf("Station date: %04u-%02u-%02u\n", (unsigned)station_date.year,
                     (unsigned)station_date.month,     (unsigned)station_date.day);
    } else {
        (void)printf("Station date unavailable: %s\n", Status_ToString(date_status));
    }
}

static void PrintStartupStatus(void) {
    const RTCState state = RTC_GetState();

    (void)printf("UART service console ready (115200 baud).\n");
    (void)printf("\nCommands: TIME | SET-UTC YYYY-MM-DDTHH:MM:SSZ | RUN\n");
    (void)printf("SET-UTC uses UTC, 24-hour time.\n");
    (void)printf("\nRTC state: %s\n", RTCState_ToString(state));

    if (state == RTC_STATE_TRUSTED) {
        (void)printf(
            "RTC time is trusted.\n"
            "Enter TIME to verify it, or RUN to calculate.\n");
    } else if (state == RTC_STATE_CLOCK_FAULT) {
        (void)printf(
            "RTC hardware is unavailable. Fix the RTC clock before calculating.\n");
    } else {
        (void)printf(
            "\nRTC time is not trusted. Close the serial terminal,\n"
            "run tools/rtc/rtc-set.py from the host, then reopen the terminal.\n");
    }
}

/* *** Command handling *** */

static bool HandleLine(const char *line) {
    if (strcmp(line, "TIME") == 0) {
        Console_PrintCurrentTime();
        return false;
    }

    if (strncmp(line, CMD_SET_PREFIX, sizeof(CMD_SET_PREFIX) - 1U) == 0) {
        uint32_t timestamp = 0U;

        Status status = ParseUtcTimestamp(
            &line[sizeof(CMD_SET_PREFIX) - 1U], &timestamp);

        if (status != STATUS_OK) {
            (void)printf("\nSET-UTC: %s\n", Status_ToString(status));
            (void)printf(
                "Expected: SET-UTC YYYY-MM-DDTHH:MM:SSZ (UTC).\n");
            return false;
        }

        status = RTC_SetEpoch(timestamp);

        /* This response line is parsed by tools/rtc/rtc-set.py */
        (void)printf("\nSET-UTC: %s\n", Status_ToString(status));

        if (status == STATUS_OK) {
            (void)printf("\nRTC set and hardware read-back verified.\n");
        }

        Console_PrintCurrentTime();
        return false;
    }

    if (strcmp(line, "RUN") == 0) {
        return true;
    }

    (void)printf(
        "\nCommand rejected. Use TIME, SET-UTC YYYY-MM-DDTHH:MM:SSZ, or RUN.\n");

    return false;
}

static void ResetLine(void) {
    s_length     = 0U;
    s_line_error = LINE_ERROR_NONE;
}

static bool ProcessCharacter(const uint8_t character) {
    bool run_requested = false;

    if ((character == '\r') || (character == '\n')) {
        if (s_line_error == LINE_ERROR_TOO_LONG) {
            (void)printf("Command rejected: line too long.\n");
        } else if (s_line_error == LINE_ERROR_RX_OVERRUN) {
            (void)printf(
                "Command rejected: UART RX buffer overrun; resend the command.\n");
        } else if (s_length  > 0U) {
            s_line[s_length] = '\0';
            run_requested    = HandleLine(s_line);
        }

        ResetLine();
        return run_requested;
    }

    /* Ignore the rest of a damaged line until its terminator arrives */
    if (s_line_error != LINE_ERROR_NONE) {
        return false;
    }

    if (s_length < (LINE_CAPACITY - 1U)) {
        s_line[s_length] = (char)character;
        ++s_length;
    } else {
        s_line_error = LINE_ERROR_TOO_LONG;
    }

    return false;
}

/* *** Public console API *** */

void Console_Init(void) {
    s_length     = 0U;
    s_line_error = LINE_ERROR_NONE;
    s_uart_ready = false;

    ring_buf_init(&s_rx_ring, sizeof(s_rx_storage), s_rx_storage);
    atomic_clear(&s_rx_overrun);

    if (!device_is_ready(s_uart_dev)) {
        (void)printf("ERROR: Zephyr console UART device is not ready.\n");
        return;
    }

    const int callback_status =
        uart_irq_callback_user_data_set(s_uart_dev, UartRxCallback, NULL);

    if (callback_status != 0) {
        (void)printf("ERROR: UART RX callback setup failed: %d\n", callback_status);
        return;
    }

    uart_irq_rx_enable(s_uart_dev);
    s_uart_ready = true;

    PrintStartupStatus();
}

bool Console_Poll(void) {
    if (!s_uart_ready) {
        return false;
    }

    bool    run_requested = false;
    uint8_t character     = 0U;

    /* Reject the current line if the ISR had to drop received bytes */
    if (atomic_cas(&s_rx_overrun, 1, 0)) {
        s_length     = 0U;
        s_line_error = LINE_ERROR_RX_OVERRUN;
    }

    while (ring_buf_get(&s_rx_ring, &character, 1U) == 1U) {
        /* Check again in case an RX interrupt occurred while parsing */
        if (atomic_cas(&s_rx_overrun, 1, 0)) {
            s_length     = 0U;
            s_line_error = LINE_ERROR_RX_OVERRUN;
        }

        if (ProcessCharacter(character)) {
            run_requested = true;
        }
    }

    return run_requested;
}
