import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from ..climate import AirConditioner, midea_ac_ns

UseFahrenheitSwitch = midea_ac_ns.class_("UseFahrenheitSwitch", switch.Switch)

CONF_MIDEA_AC_ID = "midea_ac_id"
ICON_THERMOMETER = "mdi:thermometer"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MIDEA_AC_ID): cv.use_id(AirConditioner),
        cv.Required("use_fahrenheit"): switch.switch_schema(
            UseFahrenheitSwitch,
            icon=ICON_THERMOMETER,
            entity_category=ENTITY_CATEGORY_CONFIG,
            default_restore_mode="DISABLED",
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_AC_ID])
    sw_var = await switch.new_switch(config["use_fahrenheit"])
    await cg.register_parented(sw_var, parent)
    cg.add(parent.set_use_fahrenheit_switch(sw_var))
