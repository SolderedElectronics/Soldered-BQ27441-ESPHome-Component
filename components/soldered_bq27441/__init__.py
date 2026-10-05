import esphome.codegen as cg
from esphome.components import i2c

CODEOWNERS = ["@SolderedElectronics"]

soldered_bq27441_ns = cg.esphome_ns.namespace("soldered_bq27441")
SolderedBQ27441Component = soldered_bq27441_ns.class_(
    "SolderedBQ27441Component", cg.PollingComponent, i2c.I2CDevice
)
