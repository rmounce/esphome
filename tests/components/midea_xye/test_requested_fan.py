"""Compile and execute production XYE behavior with native UART/scheduler shims."""

from pathlib import Path
import subprocess

import pytest

HERE = Path(__file__).parent
ROOT = HERE.parents[2]


@pytest.fixture(scope="module")
def native_harness(tmp_path_factory):
    build = tmp_path_factory.mktemp("xye-native")
    # Shadow framework headers only, never the component or climate enums.
    headers = [
        "components/climate/climate.h",
        "components/climate/climate_traits.h",
        "components/number/number.h",
        "components/switch/switch.h",
        "components/binary_sensor/binary_sensor.h",
        "components/sensor/sensor.h",
        "components/text_sensor/text_sensor.h",
        "components/uart/uart.h",
        "core/component.h",
    ]
    for name in headers:
        header = build / "esphome" / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "framework.h"\n')
    (build / "esphome/core/defines.h").write_text("#pragma once\n")
    (build / "esphome/core/log.h").write_text('#include "native_log.h"\n')
    executable = build / "requested-fan"
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-DUSE_ARDUINO",
            "-DUSE_TEXT_SENSOR",
            "-DUSE_BINARY_SENSOR",
            "-DUSE_SWITCH",
            "-I",
            str(build),
            "-I",
            str(HERE / "native"),
            "-I",
            str(ROOT),
            str(ROOT / "esphome/components/midea_xye/air_conditioner.cpp"),
            str(HERE / "native/requested_fan.cpp"),
            "-o",
            str(executable),
        ],
        check=True,
    )
    return executable


@pytest.mark.parametrize(
    "scenario",
    [
        "requests",
        "queued",
        "partial",
        "full-auto",
        "off",
        "startup",
        "diagnostics",
        "fahrenheit",
        "pressure",
    ],
)
def test_requested_fan(native_harness, scenario):
    subprocess.run([str(native_harness), scenario], check=True)
