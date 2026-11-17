/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Tim Alexeenko
 * E-mail: <cloclacordis@proton.me> */

#include <math.h>
#include "validation.h"
#include "../034-math-utils/math-utils.h"

bool ValidTemperatureC(const double value) {
    return isfinite(value)
        && (value >= -100.0)
        && (value <= 100.0);
}

bool ValidHumidityPercent(const double value) {
    return isfinite(value)
        && (value >= 0.0)
        && (value <= 100.0);
}

bool ValidLatitudeRad(const double phi) {
    return isfinite(phi)
        && (phi >= -(PI / 2.0))
        && (phi <=  (PI / 2.0));
}

bool ValidDayOfYear(const uint16_t J) {
    return (J >= 1U) && (J <= 366U);
}
