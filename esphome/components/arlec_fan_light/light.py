import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import (
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_GAMMA_CORRECT,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_OUTPUT_ID,
    CONF_SWITCH_DATAPOINT,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)

from esphome.components.tuya import CONF_TUYA_ID, Tuya

DEPENDENCIES = ["tuya"]

CONF_COLOR_TEMPERATURE_DATAPOINT = "color_temperature_datapoint"
CONF_COLOR_TEMPERATURE_INVERT = "color_temperature_invert"
CONF_COLOR_TEMPERATURE_MAX_VALUE = "color_temperature_max_value"
CONF_DIMMER_DATAPOINT = "dimmer_datapoint"
CONF_ECHO_SUPPRESS_DURATION = "echo_suppress_duration"
CONF_PAIR_TIMEOUT = "pair_timeout"

arlec_fan_light_ns = cg.esphome_ns.namespace("arlec_fan_light")
ArlecFanLight = arlec_fan_light_ns.class_(
    "ArlecFanLight", light.LightOutput, cg.Component
)


def validate_min_max(config):
    if config[CONF_MIN_VALUE] > config[CONF_MAX_VALUE]:
        raise cv.Invalid(
            f"{CONF_MIN_VALUE} must be less than or equal to {CONF_MAX_VALUE}"
        )
    return config


CONFIG_SCHEMA = cv.All(
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(ArlecFanLight),
            cv.GenerateID(CONF_TUYA_ID): cv.use_id(Tuya),
            cv.Required(CONF_DIMMER_DATAPOINT): cv.uint8_t,
            cv.Required(CONF_COLOR_TEMPERATURE_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_SWITCH_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_COLOR_TEMPERATURE_INVERT, default=False): cv.boolean,
            cv.Optional(CONF_MIN_VALUE, default=0): cv.int_range(min=0),
            cv.Optional(CONF_MAX_VALUE, default=255): cv.positive_int,
            cv.Optional(CONF_COLOR_TEMPERATURE_MAX_VALUE, default=255): cv.positive_int,
            cv.Required(CONF_COLD_WHITE_COLOR_TEMPERATURE): cv.color_temperature,
            cv.Required(CONF_WARM_WHITE_COLOR_TEMPERATURE): cv.color_temperature,
            cv.Optional(CONF_ECHO_SUPPRESS_DURATION, default="1s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=cv.TimePeriod(milliseconds=60000)),
            ),
            cv.Optional(CONF_PAIR_TIMEOUT, default="200ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=cv.TimePeriod(milliseconds=5000)),
            ),
            # The Tuya MCU handles its own output curve; keep HA values linear.
            cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
            cv.Optional(
                CONF_DEFAULT_TRANSITION_LENGTH, default="0s"
            ): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_min_max,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)

    parent = await cg.get_variable(config[CONF_TUYA_ID])
    cg.add(var.set_tuya_parent(parent))
    cg.add(var.set_dimmer_id(config[CONF_DIMMER_DATAPOINT]))
    cg.add(var.set_color_temperature_id(config[CONF_COLOR_TEMPERATURE_DATAPOINT]))

    if CONF_SWITCH_DATAPOINT in config:
        cg.add(var.set_switch_id(config[CONF_SWITCH_DATAPOINT]))

    cg.add(var.set_color_temperature_invert(config[CONF_COLOR_TEMPERATURE_INVERT]))
    cg.add(var.set_min_value(config[CONF_MIN_VALUE]))
    cg.add(var.set_max_value(config[CONF_MAX_VALUE]))
    cg.add(var.set_color_temperature_max_value(config[CONF_COLOR_TEMPERATURE_MAX_VALUE]))
    cg.add(var.set_cold_white_temperature(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_warm_white_temperature(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
    cg.add(
        var.set_echo_suppress_duration(
            config[CONF_ECHO_SUPPRESS_DURATION].total_milliseconds
        )
    )
    cg.add(var.set_pair_timeout(config[CONF_PAIR_TIMEOUT].total_milliseconds))
