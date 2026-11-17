/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Tim Alexeenko
 * E-mail: <cloclacordis@proton.me> */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/kernel.h>

#include "03-validation/033-status/status.h"
#include "05-orchestration/daily-cycle.h"

#include "service-console.h"
#include "rtc-service.h"

#define STATUS_LED_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS(STATUS_LED_NODE, okay)
#error "The selected board must provide the led0 Devicetree alias"
#endif

static const struct gpio_dt_spec s_status_led = GPIO_DT_SPEC_GET(STATUS_LED_NODE, gpios);
static bool s_status_led_ready;

static void StatusLed_Init(void) {
    if (!gpio_is_ready_dt(&s_status_led)) {
        (void)printf("WARNING: status LED GPIO controller is not ready.\n");
        s_status_led_ready = false;
        return;
    }

    const int ret = gpio_pin_configure_dt(&s_status_led, GPIO_OUTPUT_INACTIVE);

    if (ret < 0) {
        (void)printf("WARNING: status LED configuration failed: %d\n", ret);
        s_status_led_ready = false;
        return;
    }

    s_status_led_ready = true;
}

static void StatusLed_Set(const bool active) {
    if (!s_status_led_ready) {
        return;
    }

    const int ret = gpio_pin_set_dt(&s_status_led, active ? 1 : 0);

    if (ret < 0) {
        (void)printf("WARNING: status LED update failed: %d\n", ret);
        s_status_led_ready = false;
    }
}

static void ExecuteDailyCycle(DailyResults *results) {
    const RTCState rtc_state = RTC_GetState();

    if (rtc_state != RTC_STATE_TRUSTED) {
        (void)printf(
            "\nCalculation not started: RTC state is %s.\n"
            "Set/verify UTC time first.\n",
            RTCState_ToString(rtc_state));
        return;
    }

    (void)printf("\nRUN accepted, starting RunDailyCycle...\n");

    const char *failed_step = "unknown";
    StatusLed_Set(true);

    const uint32_t cycles_start = k_cycle_get_32();
    const Status status = RunDailyCycle(results, &failed_step);
    const uint32_t cycles_elapsed = k_cycle_get_32() - cycles_start;

    StatusLed_Set(false);

    const uint64_t elapsed_us =
        k_cyc_to_us_floor64((uint64_t)cycles_elapsed);

    (void)printf(
        "\nRunDailyCycle: %s (%lu cycles, %lu.%03lu ms)\n", Status_ToString(status),
        (unsigned long)cycles_elapsed, (unsigned long)(elapsed_us / 1000U),
        (unsigned long)(elapsed_us % 1000U));

    /* Print the clock and station date after the calculation, as in v01x */
    Console_PrintCurrentTime();

    if (status != STATUS_OK) {
        (void)fprintf(stderr,
                      "\nRunDailyCycle failed at %s: %s.\n", failed_step, Status_ToString(status));
        return;
    }

    (void)printf(
        "\nRunDailyCycle OK\n"
        "Station date used by calculation: %04u-%02u-%02u\n\n",
        (unsigned)results->date.year, (unsigned)results->date.month, (unsigned)results->date.day);

    PrintReport(results);
}

int main(void) {
    (void)printf("\nEvapotranspiration Firmware | ZEPHYR 4.5 | STM32F446RE\n");

    StatusLed_Init();

    const Status rtc_status = RTC_Init();
    (void)printf("\nRTC init: %s\n", Status_ToString(rtc_status));

    Console_Init();

    static DailyResults results;

    for (;;) {
        if (Console_Poll()) {
            ExecuteDailyCycle(&results);
        } else {
            /* RX is interrupt-driven, so sleeping does not lose UART bytes */
            k_sleep(K_MSEC(10));
        }
    }

    return 0;
}
