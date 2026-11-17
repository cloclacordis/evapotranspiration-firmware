/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Tim Alexeenko
 * E-mail: <cloclacordis@proton.me> */

#ifndef SERVICE_CONSOLE_H
#define SERVICE_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Initializes the service console and prints startup information.
 */
void Console_Init(void);

/**
 * @brief Processes received input and handles complete commands.
 *
 * @return true if a RUN command was received; false otherwise.
 */
bool Console_Poll(void);

/**
 * @brief Prints the current RTC state, UTC time, and station date.
 */
void Console_PrintCurrentTime(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_CONSOLE_H */
