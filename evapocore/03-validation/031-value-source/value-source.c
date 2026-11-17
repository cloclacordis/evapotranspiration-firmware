/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Tim Alexeenko
 * E-mail: <cloclacordis@proton.me> */

#include "value-source.h"

const char* SensorValueSource_ToString(const SensorValueSource source) {
    switch (source) {
        case SENSOR_VALUE_MEASURED:
            return "MEASURED";
        case SENSOR_VALUE_DEFAULT:
            return "DEFAULT";
        default:
            return "UNKNOWN";
    }
}
