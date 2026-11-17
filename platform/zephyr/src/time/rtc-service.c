/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Tim Alexeenko
 * E-mail: <cloclacordis@proton.me> */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include <soc.h>
#include <stm32_ll_rcc.h>
#include <stm32_ll_rtc.h>

#include "rtc-service.h"

#include "02-providers/023-time/calendar-time.h"
#include "02-providers/023-time/time-provider.h"

/* Zephyr devices */
#define RTC_NODE   DT_NODELABEL(rtc)
#define BBRAM_NODE DT_NODELABEL(bbram)

static const struct device *const s_rtc_dev   = DEVICE_DT_GET(RTC_NODE);
static const struct device *const s_bbram_dev = DEVICE_DT_GET(BBRAM_NODE);

/* STM32F446 RTC configuration */
#define RTC_PREDIV_A 127U
#define RTC_PREDIV_S 255U

/* Trust data in STM32 backup registers, addressed through BBRAM */
#define BKP_TRUST_MARKER_OFFSET 0U
#define BKP_LAST_TRUSTED_OFFSET 4U

#define TRUST_MARKER UINT32_C(0x52544332)

/* RTC liveness check */
#define RTC_LIVENESS_TIMEOUT_MS 25U
#define RTC_SAMPLE_INTERVAL_US  1000U

#define READBACK_TOLERANCE_S    2U
#define SECONDS_PER_DAY         86400U

/* Serializes access to RTC state and backup registers;
 * the service API is intended to be called from thread context */
K_MUTEX_DEFINE(s_rtc_service_lock);

static bool s_clock_alive = false;

/* *** Backup register access *** */

static bool BackupReadU32(const size_t offset, uint32_t *out_value) {
    if ((out_value == NULL) || !device_is_ready(s_bbram_dev)) {
        return false;
    }

    uint8_t bytes[sizeof(uint32_t)];

    if (bbram_read(s_bbram_dev, offset, sizeof(bytes), bytes) != 0) {
        return false;
    }

    *out_value = sys_get_le32(bytes);

    return true;
}

static bool BackupWriteU32(const size_t offset, const uint32_t value) {
    if (!device_is_ready(s_bbram_dev)) {
        return false;
    }

    uint8_t bytes[sizeof(uint32_t)];
    sys_put_le32(value, bytes);

    return bbram_write(s_bbram_dev, offset, sizeof(bytes), bytes) == 0;
}

static bool InvalidateTrustMarker(void) {
    return BackupWriteU32(BKP_TRUST_MARKER_OFFSET, 0U);
}

/* *** Hardware diagnostics *** */

/* Functions only inspect hardware state:
 * RTC clock configuration and calendar management are owned by the
 * Zephyr STM32 RTC driver; this service does not write RCC or RTC registers */
 
static bool RtcClockSourceIsLse(void) {
    return (LL_RCC_GetRTCClockSource() == LL_RCC_RTC_CLKSOURCE_LSE)
           && ((RCC->BDCR & RCC_BDCR_LSEBYP) == 0U);
}

static bool RtcClockIsReady(void) {
    return ((RCC->BDCR & RCC_BDCR_LSEON) != 0U)
           && (LL_RCC_LSE_IsReady()      != 0U)
           && (LL_RCC_IsEnabledRTC()     != 0U);
}

/* Checks that the RTC subsecond counter actually changes;
 * reading SSR followed by DR releases the shadow-register lock when
 * shadow registers are not bypassed. It is harmless when BYPSHAD is enabled;
 * this is a read-only liveness diagnostic, not a second RTC driver */
static bool RtcSubsecondIsTicking(void) {
    const int64_t start_ms = k_uptime_get();

    uint32_t previous = RTC->SSR;
    (void)RTC->DR;

    do {
        k_busy_wait(RTC_SAMPLE_INTERVAL_US);

        const uint32_t current = RTC->SSR;
        (void)RTC->DR;

        if (current != previous) {
            return true;
        }

        previous = current;

    } while ((k_uptime_get() - start_ms) < RTC_LIVENESS_TIMEOUT_MS);

    return false;
}

/* The Zephyr 4.5 STM32 RTC driver configures:
 * 24-hour format;
 * BYPSHAD enabled;
 * prescalers from Devicetree
 * (we verify the configuration but do not modify it here) */
static bool RtcCalendarConfigurationMatches(void) {
    return (LL_RTC_GetAsynchPrescaler(RTC)   == RTC_PREDIV_A)
           && (LL_RTC_GetSynchPrescaler(RTC) == RTC_PREDIV_S)
           && ((RTC->CR & RTC_CR_FMT)        == 0U)
           && ((RTC->CR & RTC_CR_BYPSHAD)    != 0U);
}

/* Called with s_rtc_service_lock held */
static bool RefreshClockHealthLocked(void) {
    if (!s_clock_alive) {
        return false;
    }

    if (!RtcClockSourceIsLse()
        || !RtcClockIsReady()
        || !RtcSubsecondIsTicking()) {

        s_clock_alive = false;
        (void)InvalidateTrustMarker();

        return false;
    }

    return true;
}

/* *** Calendar conversion *** */

/* Reads the Zephyr RTC calendar and converts it to UTC Unix seconds; returns:
 * 0          Calendar successfully read and converted;
 * -ENODATA   RTC calendar has not been initialized;
 * Other      RTC read or calendar validation failed */
static int ReadCalendarEpoch(uint32_t *out_epoch) {
    if (out_epoch == NULL) {
        return -EINVAL;
    }

    struct rtc_time rtc_time = {0};
    const int ret = rtc_get_time(s_rtc_dev, &rtc_time);

    if (ret != 0) {
        return ret;
    }

    /* struct rtc_time.tm_year  is years since 1900;
     * STM32F446RE RTC supports calendar years 2000 ... 2099 */
    if ((rtc_time.tm_year < 100) || (rtc_time.tm_year > 199)) {
        return -EINVAL;
    }

    const CalendarTime utc = {
        .year   = (uint16_t)(rtc_time.tm_year + 1900),
        .month  = (uint8_t)(rtc_time.tm_mon   + 1),
        .day    = (uint8_t)rtc_time.tm_mday,
        .hour   = (uint8_t)rtc_time.tm_hour,
        .minute = (uint8_t)rtc_time.tm_min,
        .second = (uint8_t)rtc_time.tm_sec
    };

    uint32_t epoch = 0U;
    const Status status = CalendarTime_ToEpoch(&utc, &epoch);

    if (status != STATUS_OK) {
        return -EINVAL;
    }

    *out_epoch = epoch;

    return 0;
}

/* *** Trust evaluation *** */

/* Evaluates the calendar and backup trust data;
 * clock source and liveness are checked separately by
 * the public service functions before this function is called;
 * the output timestamp is written only for RTC_STATE_TRUSTED */
static RTCState EvaluateCalendarTrust(uint32_t *out_epoch) {
    uint32_t  epoch = 0U;
    const int read_status = ReadCalendarEpoch(&epoch);

    if (read_status == -ENODATA) {
        return RTC_STATE_NOT_SET;
    }

    if (read_status != 0) {
        return RTC_STATE_UNCONFIRMED;
    }

    if (!RtcCalendarConfigurationMatches()) {
        return RTC_STATE_UNCONFIRMED;
    }

    if ((epoch < TIME_PROVIDER_MIN_EPOCH) ||
        (epoch > TIME_PROVIDER_MAX_EPOCH)) {
        return RTC_STATE_UNCONFIRMED;
    }

    uint32_t marker = 0U;
    uint32_t last_trusted_epoch = 0U;

    if (!BackupReadU32(BKP_TRUST_MARKER_OFFSET, &marker) ||
        !BackupReadU32(BKP_LAST_TRUSTED_OFFSET, &last_trusted_epoch)) {
        return RTC_STATE_UNCONFIRMED;
    }

    if (marker != TRUST_MARKER) {
        return RTC_STATE_UNCONFIRMED;
    }

    if ((last_trusted_epoch < TIME_PROVIDER_MIN_EPOCH) ||
        (last_trusted_epoch > TIME_PROVIDER_MAX_EPOCH) ||
        (epoch < last_trusted_epoch)) {
        return RTC_STATE_UNCONFIRMED;
    }

    if (out_epoch != NULL) {
        *out_epoch = epoch;
    }

    return RTC_STATE_TRUSTED;
}

/* Called with s_rtc_service_lock held */
static RTCState GetStateLocked(uint32_t *out_epoch) {
    if (!RefreshClockHealthLocked()) {
        return RTC_STATE_CLOCK_FAULT;
    }

    const RTCState state = EvaluateCalendarTrust(out_epoch);

    /* Fail closed: an untrusted calendar must not become trusted later
     * merely because its value catches up with the last trusted timestamp */
    if (state != RTC_STATE_TRUSTED) {
        (void)InvalidateTrustMarker();
    }

    return state;
}

/* *** Public RTC service API *** */

Status RTC_Init(void) {
    k_mutex_lock(&s_rtc_service_lock, K_FOREVER);
    s_clock_alive = false;

    if (!device_is_ready(s_rtc_dev)) {
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_UNAVAILABLE;
    }

    /* RTCSEL is configured by the Zephyr RTC driver from Devicetree;
     * it cannot be changed without a backup-domain reset */
    if (!RtcClockSourceIsLse()) {
        (void)InvalidateTrustMarker();
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_INTERNAL_ERROR;
    }

    if (!RtcClockIsReady() || !RtcSubsecondIsTicking()) {
        (void)InvalidateTrustMarker();
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_UNAVAILABLE;
    }

    s_clock_alive = true;
    k_mutex_unlock(&s_rtc_service_lock);

    return STATUS_OK;
}

RTCState RTC_GetState(void) {
    k_mutex_lock(&s_rtc_service_lock, K_FOREVER);
    const RTCState state = GetStateLocked(NULL);
    k_mutex_unlock(&s_rtc_service_lock);

    return state;
}

const char *RTCState_ToString(const RTCState state) {
    switch (state) {
        case RTC_STATE_CLOCK_FAULT:
            return "RTC_STATE_CLOCK_FAULT";
        case RTC_STATE_NOT_SET:
            return "RTC_STATE_NOT_SET";
        case RTC_STATE_UNCONFIRMED:
            return "RTC_STATE_UNCONFIRMED";
        case RTC_STATE_TRUSTED:
            return "RTC_STATE_TRUSTED";
        default:
            return "RTC_STATE_UNKNOWN";
    }
}

Status RTC_ReadEpoch(uint32_t *out_epoch) {
    if (out_epoch == NULL) {
        return STATUS_NULL_POINTER;
    }

    k_mutex_lock(&s_rtc_service_lock, K_FOREVER);
    const RTCState state = GetStateLocked(out_epoch);
    k_mutex_unlock(&s_rtc_service_lock);

    if (state != RTC_STATE_TRUSTED) {
        return STATUS_UNAVAILABLE;
    }

    return STATUS_OK;
}

Status RTC_SetEpoch(const uint32_t epoch_utc) {
    if ((epoch_utc < TIME_PROVIDER_MIN_EPOCH) ||
        (epoch_utc > TIME_PROVIDER_MAX_EPOCH)) {
        return STATUS_INVALID_VALUE;
    }

    k_mutex_lock(&s_rtc_service_lock, K_FOREVER);

    if (!RefreshClockHealthLocked()) {
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_UNAVAILABLE;
    }

    if (!RtcCalendarConfigurationMatches()) {
        (void)InvalidateTrustMarker();
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_INTERNAL_ERROR;
    }

    if (!device_is_ready(s_bbram_dev)) {
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_UNAVAILABLE;
    }

    /* Invalidate trust before touching the calendar;
     * if any later operation fails, the time remains untrusted */
    if (!InvalidateTrustMarker()) {
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_INTERNAL_ERROR;
    }

    CalendarTime utc;
    const Status conversion = CalendarTime_FromEpoch(epoch_utc, 0, &utc);

    if (conversion != STATUS_OK) {
        k_mutex_unlock(&s_rtc_service_lock);
        return conversion;
    }

    /* Zephyr rtc_time conventions:
     *   tm_year: years since 1900;
     *   tm_mon:  months since January, 0 ... 11;
     *   tm_wday: Sunday = 0 ... Saturday = 6;
     * 1970-01-01 was  Thursday, tm_wday  = 4 */
    const struct rtc_time set_time = {
        .tm_sec   = utc.second,
        .tm_min   = utc.minute,
        .tm_hour  = utc.hour,
        .tm_mday  = utc.day,
        .tm_mon   = (int)utc.month - 1,
        .tm_year  = (int)utc.year  - 1900,
        .tm_wday  = (int)(((epoch_utc / SECONDS_PER_DAY) + 4U) % 7U),
        .tm_yday  = -1,
        .tm_isdst = -1,
        .tm_nsec  = 0
    };

    const int set_status = rtc_set_time(s_rtc_dev, &set_time);

    if (set_status != 0) {
        (void)InvalidateTrustMarker();
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_UNAVAILABLE;
    }

    /* Read the calendar back and verify the written time */
    uint32_t readback_epoch = 0U;

    if ((ReadCalendarEpoch(&readback_epoch) != 0)
        || (readback_epoch  < epoch_utc)
        || ((readback_epoch - epoch_utc) > READBACK_TOLERANCE_S)) {

        (void)InvalidateTrustMarker();
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_INTERNAL_ERROR;
    }

    /* Confirm that the clock is still ticking after the calendar update */
    if (!RefreshClockHealthLocked()) {
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_UNAVAILABLE;
    }

    /* Persist the timestamp first;
     * the trust marker is the commit record and is written last */
    if (!BackupWriteU32(BKP_LAST_TRUSTED_OFFSET, epoch_utc)) {
        (void)InvalidateTrustMarker();
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_INTERNAL_ERROR;
    }

    uint32_t stored_epoch = 0U;

    if (!BackupReadU32(BKP_LAST_TRUSTED_OFFSET, &stored_epoch) ||
        (stored_epoch != epoch_utc)) {

        (void)InvalidateTrustMarker();
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_INTERNAL_ERROR;
    }

    if (!BackupWriteU32(BKP_TRUST_MARKER_OFFSET, TRUST_MARKER)) {
        (void)InvalidateTrustMarker();
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_INTERNAL_ERROR;
    }

    /* Final validation after committing the trust marker */
    uint32_t final_epoch = 0U;
    const RTCState final_state = EvaluateCalendarTrust(&final_epoch);

    if ((final_state != RTC_STATE_TRUSTED)
        || (final_epoch  < epoch_utc)
        || ((final_epoch - epoch_utc) > READBACK_TOLERANCE_S)) {

        (void)InvalidateTrustMarker();
        k_mutex_unlock(&s_rtc_service_lock);
        return STATUS_INTERNAL_ERROR;
    }

    k_mutex_unlock(&s_rtc_service_lock);

    return STATUS_OK;
}
