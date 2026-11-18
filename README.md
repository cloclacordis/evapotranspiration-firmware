# Evapotranspiration Firmware

[![Host CI](https://github.com/cloclacordis/evapotranspiration-firmware/actions/workflows/host-ci.yml/badge.svg)](https://github.com/cloclacordis/evapotranspiration-firmware/actions/workflows/host-ci.yml) [![Zephyr Build](https://github.com/cloclacordis/evapotranspiration-firmware/actions/workflows/zephyr-build.yml/badge.svg)](https://github.com/cloclacordis/evapotranspiration-firmware/actions/workflows/zephyr-build.yml) [![License: Apache-2.0](https://img.shields.io/badge/License-Apache--2.0-blue.svg)](LICENSE)

## About

**Evapotranspiration Firmware** is a Zephyr-based embedded application for calculating reference evapotranspiration according to the [`FAO-56 Penman–Monteith methodology`](https://agrhysmo.agr.unipi.it/wp-content/uploads/2025/09/FAO56%202025.pdf).

The firmware is built around a portable C17 computation core originally developed and documented as [`EvapoCore`](https://github.com/cloclacordis/evapocore). The computation core remains independent of Zephyr; the platform layer provides the target-specific services.

The current baseline targets the **STM32F446RE Nucleo-64** and **Zephyr RTOS 4.5.0**. The application is **in active development** (sensor inputs are currently emulated).

## Build and run

This repository is a Zephyr application managed by West. Its `west.yml` manifest defines the workspace dependencies and places the application at `applications/evapotranspiration-firmware`.

Create and activate a Python virtual environment, install West, and initialize the workspace:

```bash
python3 -m venv ~/zephyrproject/.venv
source ~/zephyrproject/.venv/bin/activate
python -m pip install west
```

```bash
west init -m https://github.com/cloclacordis/evapotranspiration-firmware --mr main ~/zephyrproject
cd ~/zephyrproject
west update
west packages pip --install
west zephyr-export
```

Install the Zephyr SDK with the `arm-zephyr-eabi` toolchain as described in the [`Zephyr Getting Started Guide`](https://docs.zephyrproject.org/4.5.0/develop/getting_started/index.html).

Build and flash from the application directory:

```bash
source ~/zephyrproject/.venv/bin/activate
cd ~/zephyrproject/applications/evapotranspiration-firmware
west build -p always -b nucleo_f446re -d build-zephyr .
west flash -d build-zephyr --runner openocd
```

The RTC must be set to UTC before a daily calculation can run. Install the commissioning utility’s Python dependencies and run it with the board’s serial port:

```bash
source ~/zephyrproject/.venv/bin/activate
python -m pip install -r tools/rtc/requirements.txt
python tools/rtc/rtc-set.py --port /dev/ttyACM0
```

See [`devlog`](docs/devlogs/devlog01-zephyr-baseline-port.md) for details of the build, flashing, and hardware verification.

## Computation core build, run, and test

EvapoCore can also be built and tested on the host (requires CMake 3.28 or later and a C17-compatible compiler):

```bash
cmake -S evapocore -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To run the FAO-56 computation core test suite with AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake -S evapocore -B build-sanitize -DENABLE_SANITIZERS=ON
cmake --build build-sanitize --target fao56_test
ctest --test-dir build-sanitize -R '^fao56_suite$' --output-on-failure
```

## License

This repository is distributed under the [`Apache License 2.0`](LICENSE). EvapoCore is also available under AGPL-3.0 in the [`original repository`](https://github.com/cloclacordis/evapocore). Third-party components remain under their original licenses.
