/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Tim Alexeenko
 * E-mail: <cloclacordis@proton.me> */

#ifndef RTC_SERVICE_H
#define RTC_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "03-validation/033-status/status.h"

/** @brief RTC calendar and time trust state. */
typedef enum {
    RTC_STATE_CLOCK_FAULT = 0, /**< RTC clock is unavailable or not ticking. */
    RTC_STATE_NOT_SET,         /**< Clock is operational, but calendar is not initialized. */
    RTC_STATE_UNCONFIRMED,     /**< Calendar is initialized, but time cannot be trusted. */
    RTC_STATE_TRUSTED          /**< Calendar and time pass all trust checks. */
} RTCState;

/**
 * @brief Initializes the RTC service and checks clock operation.
 *
 * @retval STATUS_OK             RTC clock is operational.
 * @retval STATUS_UNAVAILABLE    RTC device or clock is unavailable.
 * @retval STATUS_INTERNAL_ERROR RTC clock configuration is invalid.
 */
Status RTC_Init(void);

/**
 * @brief Returns the current RTC state.
 *
 * @return Current RTCState.
 */
RTCState RTC_GetState(void);

/**
 * @brief Returns a human-readable name for an RTC state.
 *
 * @param[in] state RTC state.
 * @return Static string representing the state.
 */
const char *RTCState_ToString(RTCState state);

/**
 * @brief Reads the current trusted UTC timestamp.
 *
 * @param[out] out_epoch Unix timestamp in seconds. Must not be NULL.
 *
 * @retval STATUS_OK           Timestamp successfully read.
 * @retval STATUS_NULL_POINTER out_epoch is NULL.
 * @retval STATUS_UNAVAILABLE  RTC time is unavailable or untrusted.
 */
Status RTC_ReadEpoch(uint32_t *out_epoch);

/**
 * @brief Sets the RTC calendar to UTC and verifies the result.
 *
 * @param[in] epoch_utc Unix timestamp in seconds.
 *
 * @retval STATUS_OK             Time successfully set and verified.
 * @retval STATUS_INVALID_VALUE  Timestamp is outside the supported range.
 * @retval STATUS_UNAVAILABLE    RTC or backup memory is unavailable.
 * @retval STATUS_INTERNAL_ERROR Operation or verification failed.
 */
Status RTC_SetEpoch(uint32_t epoch_utc);

#ifdef __cplusplus
}
#endif

#endif /* RTC_SERVICE_H */
