/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Tim Alexeenko
 * E-mail: <cloclacordis@proton.me> */

#ifndef VALUE_SOURCE_H
#define VALUE_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

/* To track the source of an input value - measurement or fallback */
typedef enum {
    SENSOR_VALUE_MEASURED = 0,
    SENSOR_VALUE_DEFAULT
} SensorValueSource;

/**
 * @brief Returns a human-readable name for a SensorValueSource value.
 *
 * @param[in] source A SensorValueSource value. Values not matching any
 *                   defined enumerator are returned as "UNKNOWN".
 *
 * @return "MEASURED", "DEFAULT", or "UNKNOWN" for an unrecognized value.
 */
const char* SensorValueSource_ToString(SensorValueSource source);

#ifdef __cplusplus
}
#endif

#endif /* VALUE_SOURCE_H */
