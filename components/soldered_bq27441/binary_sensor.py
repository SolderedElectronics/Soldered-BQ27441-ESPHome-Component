import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_BATTERY

from . import SolderedBQ27441Component

CODEOWNERS = ["@SolderedElectronics"]

CONF_SOLDERED_BQ27441_ID = "soldered_bq27441_id"

# Flags() bits, see BQ27441-G1 Technical Reference Manual section 4.4
CONF_FULLY_CHARGED = "fully_charged"  # FC
CONF_FAST_CHARGE_ALLOWED = "fast_charge_allowed"  # CHG
CONF_DISCHARGING = "discharging"  # DSG
CONF_BATTERY_LOW = "battery_low"  # SOC1
CONF_BATTERY_CRITICAL = "battery_critical"  # SOCF

ICON_BATTERY_CHARGING_100 = "mdi:battery-charging-100"
ICON_BATTERY_ARROW_DOWN = "mdi:battery-arrow-down"
ICON_BATTERY_CHARGING_HIGH = "mdi:battery-charging-high"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SOLDERED_BQ27441_ID): cv.use_id(SolderedBQ27441Component),
        cv.Optional(CONF_FULLY_CHARGED): binary_sensor.binary_sensor_schema(
            icon=ICON_BATTERY_CHARGING_100
        ),
        cv.Optional(CONF_FAST_CHARGE_ALLOWED): binary_sensor.binary_sensor_schema(
            icon=ICON_BATTERY_CHARGING_HIGH
        ),
        cv.Optional(CONF_DISCHARGING): binary_sensor.binary_sensor_schema(
            icon=ICON_BATTERY_ARROW_DOWN
        ),
        cv.Optional(CONF_BATTERY_LOW): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY
        ),
        cv.Optional(CONF_BATTERY_CRITICAL): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY
        ),
    }
)

BINARY_SENSORS = [
    CONF_FULLY_CHARGED,
    CONF_FAST_CHARGE_ALLOWED,
    CONF_DISCHARGING,
    CONF_BATTERY_LOW,
    CONF_BATTERY_CRITICAL,
]


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SOLDERED_BQ27441_ID])
    for key in BINARY_SENSORS:
        if sens_config := config.get(key):
            sens = await binary_sensor.new_binary_sensor(sens_config)
            cg.add(getattr(hub, f"set_{key}_binary_sensor")(sens))
