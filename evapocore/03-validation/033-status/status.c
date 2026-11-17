/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Tim Alexeenko
 * E-mail: <cloclacordis@proton.me> */

#include "status.h"

const char* Status_ToString(const Status status) {
    switch (status) {
        case STATUS_OK:
            return "STATUS_OK";
        case STATUS_NULL_POINTER:
            return "STATUS_NULL_POINTER";
        case STATUS_INVALID_VALUE:
            return "STATUS_INVALID_VALUE";
        case STATUS_UNAVAILABLE:
            return "STATUS_UNAVAILABLE";
        case STATUS_INTERNAL_ERROR:
            return "STATUS_INTERNAL_ERROR";
        default:
            return "STATUS_UNKNOWN";
    }
}
