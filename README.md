# Soldered BQ27441 ESPHome Component

| ![Fuel Gauge BQ27441 Breakout](https://cms.soldered.com/products/333065/media/333065_featured-photo_3281b7.jpg) |
| :------------------------------------------------------------------------------------------------------------: |
|                          [Fuel Gauge BQ27441 Breakout](https://www.solde.red/333065)                          |

The breakout is built around TI's BQ27441-G1A fuel gauge for single-cell (1S, 4.2 V) Li-ion/LiPo batteries. It measures
battery voltage, current and power, and computes state of charge (%), remaining and full capacity (mAh) and state of
health on-chip, over I2C (address 0x55). The board is part of the
[easyC / Qwiic ecosystem](https://soldered.com/collections/qwiic-ecosystem), so it connects with a single cable.

External ESPHome component for the Soldered Fuel Gauge BQ27441 breakout. It is a port of the
[Soldered BQ27441 Arduino library](https://github.com/SolderedElectronics/Soldered-BQ27441-Battery-Fuel-Gauge-Arduino-Library)
(register logic follows the [Soldered BQ27441 ESP-IDF component](https://github.com/SolderedElectronics/Soldered-BQ27441-ESP-IDF-Component))
and publishes the readings as ESPHome [sensors](https://esphome.io/components/sensor/) and the gauge's status flags as
[binary sensors](https://esphome.io/components/binary_sensor/).

> **The gauge is powered from the battery.** Connect the battery to the breakout before booting, otherwise the
> BQ27441 does not answer on I2C and the component marks itself as failed.

## Repository Contents

- **components/** - the ESPHome external component (Python config + C++ implementation)
- **examples/** - example YAML configs showing how to use the component

## Usage

Reference this repo directly from your own ESPHome YAML (no need to clone it locally):

```yaml
external_components:
  - source: github://SolderedElectronics/Soldered-BQ27441-ESPHome-Component
    components: [soldered_bq27441]

i2c:
  sda: GPIO21
  scl: GPIO22

sensor:
  - platform: soldered_bq27441
    id: fuel_gauge
    update_interval: 10s
    design_capacity: 600 # mAh, your battery's capacity
    voltage:
      name: "Battery Voltage"
    current:
      name: "Battery Current"
    battery_level:
      name: "Battery Level"
    remaining_capacity:
      name: "Battery Remaining Capacity"

binary_sensor:
  - platform: soldered_bq27441
    soldered_bq27441_id: fuel_gauge
    fully_charged:
      name: "Battery Fully Charged"
    battery_low:
      name: "Battery Low"
```

### Battery model

The gauge needs to know the battery it is measuring (design capacity, design energy, terminate voltage, taper rate).
These values live in the BQ27441's RAM and fall back to TI's ROM defaults (1340 mAh) whenever the gauge loses power,
which the gauge reports with its `ITPOR` flag. On boot the component writes the configured model **only if `ITPOR`
is set, or if the design capacity the gauge reports differs from `design_capacity`** (e.g. you changed it in YAML
while the battery stayed connected). Otherwise rebooting the ESP does not resimulate the gauge or disturb what it has
learned about the battery. If an update later finds `ITPOR` set again (battery was disconnected), the model is
reloaded automatically. After loading, the design capacity is read back and a mismatch is logged as an error. Loading
the model briefly blocks (unseal, config update mode, soft reset - typically well under a second).

Only `design_capacity` can be compared without entering config mode, so changing just `design_energy`,
`terminate_voltage` or `taper_current` is applied the next time the gauge loses power (disconnect the battery for a
few seconds to force it).

Every update logs one line at `DEBUG` level with all raw readings and the `Flags()` register, e.g.
`3912 mV, -45 mA, -176 mW, 87 %, 520/600 mAh, SoH 100 % (status 3), 24.5 °C, flags 0x0188`. ESPHome itself logs
published sensor values only at `VERBOSE` level.

### Configuration variables

Sensor platform (`sensor:`):

- **design_capacity** (**Required**, int): battery design capacity in mAh, `1` - `8000`.
- **design_energy** (*Optional*, int): battery design energy in mWh, `1` - `32767`. Defaults to
  `design_capacity` × 3.7 V (nominal cell voltage).
- **terminate_voltage** (*Optional*, int): lowest operating voltage of your circuit in mV, `2500` - `3700`. The gauge
  reports 0 % at this voltage. Defaults to `3200` (TI's default).
- **taper_current** (*Optional*, int): charge termination (taper) current of your charger in mA. Used to compute the
  gauge's Taper Rate (`design_capacity / (0.1 × taper_current)`, capped at 2000), which is how the gauge detects a full
  charge. If omitted, TI's default Taper Rate is kept.
- **voltage** (*Optional*): battery voltage in V. All options from
  [Sensor](https://esphome.io/components/sensor/#config-sensor).
- **current** (*Optional*): average battery current in A, positive while charging, negative while discharging. All
  options from [Sensor](https://esphome.io/components/sensor/#config-sensor).
- **power** (*Optional*): average battery power in W, same sign convention as `current`. All options from
  [Sensor](https://esphome.io/components/sensor/#config-sensor).
- **battery_level** (*Optional*): state of charge in %. All options from
  [Sensor](https://esphome.io/components/sensor/#config-sensor).
- **remaining_capacity** (*Optional*): remaining capacity in mAh. All options from
  [Sensor](https://esphome.io/components/sensor/#config-sensor).
- **full_capacity** (*Optional*): learned full-charge capacity in mAh. All options from
  [Sensor](https://esphome.io/components/sensor/#config-sensor).
- **state_of_health** (*Optional*): state of health in % (full-charge capacity vs. design capacity). Reported as
  unknown until the gauge has a valid estimate. All options from
  [Sensor](https://esphome.io/components/sensor/#config-sensor).
- **temperature** (*Optional*): BQ27441 die temperature in °C (the breakout has no battery thermistor). All options
  from [Sensor](https://esphome.io/components/sensor/#config-sensor).
- **address** (*Optional*, int): I2C address of the gauge. Defaults to `0x55`.
- **update_interval** (*Optional*, [Time](https://esphome.io/guides/configuration-types#config-time)): how often to
  read the gauge. Defaults to `60s`. The gauge itself measures once per second.
- **i2c_id** (*Optional*, [ID](https://esphome.io/guides/configuration-types#config-id)): I2C bus to use, if there is
  more than one.

Binary sensor platform (`binary_sensor:`), states come from the gauge's `Flags()` register:

- **soldered_bq27441_id** (*Optional*, [ID](https://esphome.io/guides/configuration-types#config-id)): ID of the
  sensor platform above, needed if there is more than one gauge.
- **fully_charged** (*Optional*): on when the gauge has detected charge termination (`FC`).
- **fast_charge_allowed** (*Optional*): on while fast charging is allowed (`CHG`). Clears when state of charge reaches
  99 % during charging and sets again at 95 % or below - it is **not** a "charger connected" indicator.
- **discharging** (*Optional*): on while the gauge detects discharge (`DSG`).
- **battery_low** (*Optional*): on while state of charge is at or below the SOC1 threshold (`SOC1`, TI default 10 %,
  clears at 15 %).
- **battery_critical** (*Optional*): on while state of charge is at or below the SOCF threshold (`SOCF`, TI default
  2 %, clears at 5 %).

All binary sensors accept the options from [Binary Sensor](https://esphome.io/components/binary_sensor/#config-binary_sensor).

See [`examples/basic.yaml`](examples/basic.yaml) for a full working example.

### Hardware design

You can find hardware design for this board in the
[_Fuel gauge BQ27441 breakout_](https://github.com/SolderedElectronics/Fuel-gauge-BQ27441-breakout-hardware-design)
hardware repository.

### Documentation

Access library documentation [here](https://docs.soldered.com/).

### About Soldered

<img src="https://raw.githubusercontent.com/SolderedElectronics/Soldered-Generic-Arduino-Library/dev/extras/Soldered-logo-color.png" alt="soldered-logo" width="500"/>

At Soldered, we design and manufacture a wide selection of electronic products to help you turn your ideas into acts and bring you one step closer to your final project. Our products are intented for makers and crafted in-house by our experienced team in Osijek, Croatia. We believe that sharing is a crucial element for improvement and innovation, and we work hard to stay connected with all our makers regardless of their skill or experience level. Therefore, all our products are open-source. Finally, we always have your back. If you face any problem concerning either your shopping experience or your electronics project, our team will help you deal with it, offering efficient customer service and cost-free technical support anytime. Some of those might be useful for you:

- [Web Store](https://www.soldered.com/shop)
- [Tutorials & Projects](https://soldered.com/learn)
- [Documentation](https://docs.soldered.com)

### Original source

This component is a port of the [Soldered BQ27441 Arduino library](https://github.com/SolderedElectronics/Soldered-BQ27441-Battery-Fuel-Gauge-Arduino-Library),
which is based on the [SparkFun BQ27441 Arduino Library](https://github.com/sparkfun/SparkFun_BQ27441_Arduino_Library). Thank you, SparkFun.

### Open-source license

Soldered invests vast amounts of time into hardware & software for these products, which are all open-source. Please support future development by buying one of our products.

Check license details in the LICENSE file. Long story short, use these open-source files for any purpose you want to, as long as you apply the same open-source licence to it and disclose the original source. No warranty - all designs in this repository are distributed in the hope that they will be useful, but without any warranty. They are provided "AS IS", therefore without warranty of any kind, either expressed or implied. The entire quality and performance of what you do with the contents of this repository are your responsibility. In no event, Soldered (TAVU) will be liable for your damages, losses, including any general, special, incidental or consequential damage arising out of the use or inability to use the contents of this repository.

## Have fun!

And thank you from your fellow makers at Soldered Electronics.
