/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Tim Alexeenko
 * E-mail: <cloclacordis@proton.me> */

#include "02-providers/023-time/time-provider.h"
#include "rtc-service.h"

Status TimeProvider_ReadTimestamp(uint32_t *out_timestamp) {
    return RTC_ReadEpoch(out_timestamp);
}
