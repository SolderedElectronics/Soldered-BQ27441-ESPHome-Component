/**
 * @file soldered_bq27441.h
 * @brief Public API for the soldered_bq27441 ESPHome component
 * @author Soldered Electronics
 *
 * Driver for the Soldered Fuel Gauge BQ27441 breakout, ported from the Soldered BQ27441 Arduino library (itself based
 * on the SparkFun BQ27441 Arduino library) and the Soldered BQ27441 ESP-IDF component. The BQ27441-G1A gauges the
 * battery on its own; on every update this component reads its standard commands (voltage, current, SoC, ...) and the
 * Flags() register.
 *
 * The battery model (design capacity/energy, terminate voltage, taper rate) lives in the gauge's RAM and falls back to
 * ROM defaults whenever the gauge loses power. Flags() [ITPOR] signals that, so the model is only written when ITPOR
 * is set (on boot and whenever an update sees it set again) or when the gauge's DesignCapacity() differs from the
 * configured one on boot, which leaves the gauge's learned state alone across ESP reboots.
 */

#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

namespace esphome {
namespace soldered_bq27441 {

class SolderedBQ27441Component : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_design_capacity(uint16_t design_capacity) { this->design_capacity_ = design_capacity; }
  void set_design_energy(uint16_t design_energy) { this->design_energy_ = design_energy; }
  void set_terminate_voltage(uint16_t terminate_voltage) { this->terminate_voltage_ = terminate_voltage; }
  void set_taper_rate(uint16_t taper_rate) { this->taper_rate_ = taper_rate; }

  void set_voltage_sensor(sensor::Sensor *sensor) { this->voltage_sensor_ = sensor; }
  void set_current_sensor(sensor::Sensor *sensor) { this->current_sensor_ = sensor; }
  void set_power_sensor(sensor::Sensor *sensor) { this->power_sensor_ = sensor; }
  void set_battery_level_sensor(sensor::Sensor *sensor) { this->battery_level_sensor_ = sensor; }
  void set_remaining_capacity_sensor(sensor::Sensor *sensor) { this->remaining_capacity_sensor_ = sensor; }
  void set_full_capacity_sensor(sensor::Sensor *sensor) { this->full_capacity_sensor_ = sensor; }
  void set_state_of_health_sensor(sensor::Sensor *sensor) { this->state_of_health_sensor_ = sensor; }
  void set_temperature_sensor(sensor::Sensor *sensor) { this->temperature_sensor_ = sensor; }

#ifdef USE_BINARY_SENSOR
  void set_fully_charged_binary_sensor(binary_sensor::BinarySensor *sensor) { this->fully_charged_sensor_ = sensor; }
  void set_fast_charge_allowed_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->fast_charge_allowed_sensor_ = sensor;
  }
  void set_discharging_binary_sensor(binary_sensor::BinarySensor *sensor) { this->discharging_sensor_ = sensor; }
  void set_battery_low_binary_sensor(binary_sensor::BinarySensor *sensor) { this->battery_low_sensor_ = sensor; }
  void set_battery_critical_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->battery_critical_sensor_ = sensor;
  }
#endif

 protected:
  bool read_word_(uint8_t reg, uint16_t *out);
  bool write_word_(uint8_t reg, uint16_t value);
  bool read_control_word_(uint16_t function, uint16_t *out);
  bool execute_control_word_(uint16_t function);
  bool compute_block_checksum_(uint8_t *out_csum);
  bool write_extended_data_(uint8_t class_id, uint8_t offset, const uint8_t *data, uint8_t len);
  bool write_extended_word_(uint8_t class_id, uint8_t offset, uint16_t value);
  bool enter_config_();
  bool exit_config_();
  bool load_battery_model_();

  uint16_t design_capacity_{0};
  uint16_t design_energy_{0};
  uint16_t terminate_voltage_{3200};
  uint16_t taper_rate_{0};  // 0 = leave the ROM default

  bool sealed_on_entry_{false};
  bool model_pending_{false};

  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *battery_level_sensor_{nullptr};
  sensor::Sensor *remaining_capacity_sensor_{nullptr};
  sensor::Sensor *full_capacity_sensor_{nullptr};
  sensor::Sensor *state_of_health_sensor_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};

#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *fully_charged_sensor_{nullptr};
  binary_sensor::BinarySensor *fast_charge_allowed_sensor_{nullptr};
  binary_sensor::BinarySensor *discharging_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_low_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_critical_sensor_{nullptr};
#endif
};

}  // namespace soldered_bq27441
}  // namespace esphome
