# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Tim Alexeenko
# E-mail: <cloclacordis@proton.me>

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(evapotranspiration_firmware)

set(EVAPOCORE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/evapocore
)

# Portable EvapoCore modules
file(GLOB_RECURSE EVAPOCORE_SOURCES CONFIGURE_DEPENDS
    "${EVAPOCORE_DIR}/01-measurement/*.c"
    "${EVAPOCORE_DIR}/02-providers/*.c"
    "${EVAPOCORE_DIR}/03-validation/*.c"
    "${EVAPOCORE_DIR}/04-calculation/*.c"
)

# Replace the host time source with the Zephyr platform adapter
list(REMOVE_ITEM EVAPOCORE_SOURCES
    "${EVAPOCORE_DIR}/02-providers/023-time/time-provider-host.c"
)

# Existing orchestration: keep the calculation/application logic unchanged
list(APPEND EVAPOCORE_SOURCES
    "${EVAPOCORE_DIR}/05-orchestration/daily-cycle.c"
)

set(PLATFORM_SOURCES
    platform/zephyr/src/time/rtc-service.c
    platform/zephyr/src/time/time-provider-zephyr.c
    platform/zephyr/src/console/service-console.c
)

target_sources(app PRIVATE
    src/main.c
    ${EVAPOCORE_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(app PRIVATE
    "${EVAPOCORE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/zephyr/include"
)

target_link_libraries(app PRIVATE m)
