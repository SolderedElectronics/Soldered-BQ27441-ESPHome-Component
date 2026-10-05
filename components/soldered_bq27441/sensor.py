import esphome.codegen as cg
from esphome.components import i2c, sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_BATTERY_LEVEL,
    CONF_CURRENT,
    CONF_ID,
    CONF_POWER,
    CONF_TEMPERATURE,
    CONF_VOLTAGE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_HEART_PULSE,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
)

from . import SolderedBQ27441Component

CODEOWNERS = ["@SolderedElectronics"]
DEPENDENCIES = ["i2c"]

CONF_REMAINING_CAPACITY = "remaining_capacity"
CONF_FULL_CAPACITY = "full_capacity"
CONF_STATE_OF_HEALTH = "state_of_health"
CONF_DESIGN_CAPACITY = "design_capacity"
CONF_DESIGN_ENERGY = "design_energy"
CONF_TERMINATE_VOLTAGE = "terminate_voltage"
CONF_TAPER_CURRENT = "taper_current"

ICON_BATTERY_CAPACITY = "mdi:battery-medium"
UNIT_MILLIAMP_HOUR = "mAh"

# Typical nominal voltage of a 1S Li-ion/LiPo cell, used to derive design_energy when it is not given
NOMINAL_CELL_VOLTAGE = 3.7


def _capacity_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_MILLIAMP_HOUR,
        icon=ICON_BATTERY_CAPACITY,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    )


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SolderedBQ27441Component),
            cv.Required(CONF_DESIGN_CAPACITY): cv.int_range(min=1, max=8000),
            cv.Optional(CONF_DESIGN_ENERGY): cv.int_range(min=1, max=32767),
            cv.Optional(CONF_TERMINATE_VOLTAGE, default=3200): cv.int_range(
                min=2500, max=3700
            ),
            cv.Optional(CONF_TAPER_CURRENT): cv.int_range(min=1, max=8000),
            cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CURRENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_AMPERE,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_POWER): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_REMAINING_CAPACITY): _capacity_schema(),
            cv.Optional(CONF_FULL_CAPACITY): _capacity_schema(),
            cv.Optional(CONF_STATE_OF_HEALTH): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                icon=ICON_HEART_PULSE,
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(i2c.i2c_device_schema(0x55))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    design_capacity = config[CONF_DESIGN_CAPACITY]
    design_energy = config.get(
        CONF_DESIGN_ENERGY, round(design_capacity * NOMINAL_CELL_VOLTAGE)
    )
    cg.add(var.set_design_capacity(design_capacity))
    cg.add(var.set_design_energy(min(design_energy, 32767)))
    cg.add(var.set_terminate_voltage(config[CONF_TERMINATE_VOLTAGE]))
    if taper_current := config.get(CONF_TAPER_CURRENT):
        # Taper Rate is in 0.1 h units: Design Capacity / (0.1 * taper current)
        cg.add(var.set_taper_rate(min(10 * design_capacity // taper_current, 2000)))

    if voltage_config := config.get(CONF_VOLTAGE):
        sens = await sensor.new_sensor(voltage_config)
        cg.add(var.set_voltage_sensor(sens))
    if current_config := config.get(CONF_CURRENT):
        sens = await sensor.new_sensor(current_config)
        cg.add(var.set_current_sensor(sens))
    if power_config := config.get(CONF_POWER):
        sens = await sensor.new_sensor(power_config)
        cg.add(var.set_power_sensor(sens))
    if battery_level_config := config.get(CONF_BATTERY_LEVEL):
        sens = await sensor.new_sensor(battery_level_config)
        cg.add(var.set_battery_level_sensor(sens))
    if remaining_capacity_config := config.get(CONF_REMAINING_CAPACITY):
        sens = await sensor.new_sensor(remaining_capacity_config)
        cg.add(var.set_remaining_capacity_sensor(sens))
    if full_capacity_config := config.get(CONF_FULL_CAPACITY):
        sens = await sensor.new_sensor(full_capacity_config)
        cg.add(var.set_full_capacity_sensor(sens))
    if state_of_health_config := config.get(CONF_STATE_OF_HEALTH):
        sens = await sensor.new_sensor(state_of_health_config)
        cg.add(var.set_state_of_health_sensor(sens))
    if temperature_config := config.get(CONF_TEMPERATURE):
        sens = await sensor.new_sensor(temperature_config)
        cg.add(var.set_temperature_sensor(sens))
