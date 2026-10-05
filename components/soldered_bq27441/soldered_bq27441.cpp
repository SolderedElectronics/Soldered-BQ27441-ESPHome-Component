/**
 * @file soldered_bq27441.cpp
 * @brief Implementation of the soldered_bq27441 ESPHome component
 * @author Soldered Electronics
 */

#include "soldered_bq27441.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome {
namespace soldered_bq27441 {

static const char *const TAG = "soldered_bq27441";

static const uint16_t DEVICE_ID = 0x0421;
static const uint16_t UNSEAL_KEY = 0x8000;

// Standard commands (2-byte, little-endian)
static const uint8_t COMMAND_CONTROL = 0x00;
static const uint8_t COMMAND_VOLTAGE = 0x04;
static const uint8_t COMMAND_FLAGS = 0x06;
static const uint8_t COMMAND_REM_CAPACITY = 0x0C;
static const uint8_t COMMAND_FULL_CAPACITY = 0x0E;
static const uint8_t COMMAND_AVG_CURRENT = 0x10;
static const uint8_t COMMAND_AVG_POWER = 0x18;
static const uint8_t COMMAND_SOC = 0x1C;
static const uint8_t COMMAND_INT_TEMP = 0x1E;
static const uint8_t COMMAND_SOH = 0x20;
static const uint8_t COMMAND_DESIGN_CAPACITY = 0x3C;  // DesignCapacity(), mirrors the State subclass value in RAM

// Control() subcommands, written to COMMAND_CONTROL then (when they return data) read back from the same address
static const uint16_t CONTROL_STATUS = 0x0000;
static const uint16_t CONTROL_DEVICE_TYPE = 0x0001;
static const uint16_t CONTROL_SET_CFGUPDATE = 0x0013;
static const uint16_t CONTROL_SEALED = 0x0020;
static const uint16_t CONTROL_SOFT_RESET = 0x0042;

static const uint16_t STATUS_SS = 1 << 13;  // CONTROL_STATUS: sealed

static const uint16_t FLAG_FC = 1 << 9;
static const uint16_t FLAG_CHG = 1 << 8;
static const uint16_t FLAG_ITPOR = 1 << 5;
static const uint16_t FLAG_CFGUPMODE = 1 << 4;
static const uint16_t FLAG_BAT_DET = 1 << 3;
static const uint16_t FLAG_SOC1 = 1 << 2;
static const uint16_t FLAG_SOCF = 1 << 1;
static const uint16_t FLAG_DSG = 1 << 0;

// Extended commands. Data inside the block-data window is big-endian (MSB first).
static const uint8_t EXTENDED_DATACLASS = 0x3E;
static const uint8_t EXTENDED_DATABLOCK = 0x3F;
static const uint8_t EXTENDED_BLOCKDATA = 0x40;
static const uint8_t EXTENDED_CHECKSUM = 0x60;
static const uint8_t EXTENDED_CONTROL = 0x61;

// State subclass: design capacity/energy, terminate voltage, taper rate
static const uint8_t ID_STATE = 82;
static const uint8_t STATE_DESIGN_CAPACITY = 10;
static const uint8_t STATE_DESIGN_ENERGY = 12;
static const uint8_t STATE_TERMINATE_VOLTAGE = 16;
static const uint8_t STATE_TAPER_RATE = 27;

static const uint16_t CONFIG_POLL_TRIES = 2000;  // ~2 s at 1 ms/try, datasheet worst-case config-mode entry/exit

static const float KELVIN_OFFSET = 273.15f;

void SolderedBQ27441Component::setup() {
  uint16_t device_id;
  if (!this->read_control_word_(CONTROL_DEVICE_TYPE, &device_id)) {
    ESP_LOGE(TAG, "No response from BQ27441 - is a battery connected? The gauge is powered from the battery");
    this->mark_failed();
    return;
  }
  if (device_id != DEVICE_ID) {
    ESP_LOGE(TAG, "Unexpected device type 0x%04X (expected 0x%04X)", device_id, DEVICE_ID);
    this->mark_failed();
    return;
  }

  uint16_t flags;
  if (!this->read_word_(COMMAND_FLAGS, &flags)) {
    this->mark_failed();
    return;
  }
  if (!(flags & FLAG_BAT_DET)) {
    ESP_LOGW(TAG, "Battery not detected (Flags() BAT_DET clear) - readings are not valid until it is");
  }
  uint16_t design_capacity;
  if (!this->read_word_(COMMAND_DESIGN_CAPACITY, &design_capacity)) {
    this->mark_failed();
    return;
  }

  // ITPOR means RAM fell back to ROM defaults. A design capacity mismatch means the gauge still holds a model from
  // an earlier config (battery never disconnected), so a changed design_capacity in YAML takes effect without that.
  if (flags & FLAG_ITPOR) {
    ESP_LOGD(TAG, "Gauge holds ROM defaults (ITPOR set)");
  } else if (design_capacity != this->design_capacity_) {
    ESP_LOGW(TAG, "Gauge holds design capacity %u mAh, configured %u mAh", design_capacity, this->design_capacity_);
  } else {
    ESP_LOGD(TAG, "Battery model already loaded (ITPOR clear, design capacity matches), not rewriting it");
    return;
  }
  if (!this->load_battery_model_())
    this->status_set_warning();
}

void SolderedBQ27441Component::update() {
  uint16_t flags;
  if (!this->read_word_(COMMAND_FLAGS, &flags)) {
    this->status_set_warning();
    return;
  }

  if ((flags & FLAG_ITPOR) || this->model_pending_) {
    if (flags & FLAG_ITPOR)
      ESP_LOGW(TAG, "Gauge was reset (ITPOR set), reloading battery model");
    if (!this->load_battery_model_()) {
      this->status_set_warning();
      return;
    }
    // Model reload resimulates SoC, re-read flags so they match the readings below
    if (!this->read_word_(COMMAND_FLAGS, &flags)) {
      this->status_set_warning();
      return;
    }
  }

  uint16_t voltage, current, power, soc, remaining_capacity, full_capacity, soh, temperature;
  if (!this->read_word_(COMMAND_VOLTAGE, &voltage) || !this->read_word_(COMMAND_AVG_CURRENT, &current) ||
      !this->read_word_(COMMAND_AVG_POWER, &power) || !this->read_word_(COMMAND_SOC, &soc) ||
      !this->read_word_(COMMAND_REM_CAPACITY, &remaining_capacity) ||
      !this->read_word_(COMMAND_FULL_CAPACITY, &full_capacity) || !this->read_word_(COMMAND_SOH, &soh) ||
      !this->read_word_(COMMAND_INT_TEMP, &temperature)) {
    ESP_LOGW(TAG, "Reading the gauge failed");
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();

  // Current and power are signed. SoH: low byte in %, high byte status (0 = not valid yet). Temperature: die, 0.1 K -
  // the breakout has no external thermistor.
  const float current_a = static_cast<int16_t>(current) / 1000.0f;
  const float power_w = static_cast<int16_t>(power) / 1000.0f;
  const float soh_percent = (soh >> 8) != 0 ? static_cast<float>(soh & 0xFF) : NAN;
  const float temperature_c = temperature / 10.0f - KELVIN_OFFSET;

  ESP_LOGD(TAG, "%u mV, %d mA, %d mW, %u %%, %u/%u mAh, SoH %u %% (status %u), %.1f °C, flags 0x%04X", voltage,
           static_cast<int16_t>(current), static_cast<int16_t>(power), soc, remaining_capacity, full_capacity,
           soh & 0xFF, soh >> 8, temperature_c, flags);

  if (this->voltage_sensor_ != nullptr)
    this->voltage_sensor_->publish_state(voltage / 1000.0f);
  if (this->current_sensor_ != nullptr)
    this->current_sensor_->publish_state(current_a);
  if (this->power_sensor_ != nullptr)
    this->power_sensor_->publish_state(power_w);
  if (this->battery_level_sensor_ != nullptr)
    this->battery_level_sensor_->publish_state(soc);
  if (this->remaining_capacity_sensor_ != nullptr)
    this->remaining_capacity_sensor_->publish_state(remaining_capacity);
  if (this->full_capacity_sensor_ != nullptr)
    this->full_capacity_sensor_->publish_state(full_capacity);
  if (this->state_of_health_sensor_ != nullptr)
    this->state_of_health_sensor_->publish_state(soh_percent);
  if (this->temperature_sensor_ != nullptr)
    this->temperature_sensor_->publish_state(temperature_c);

#ifdef USE_BINARY_SENSOR
  if (this->fully_charged_sensor_ != nullptr)
    this->fully_charged_sensor_->publish_state(flags & FLAG_FC);
  if (this->fast_charge_allowed_sensor_ != nullptr)
    this->fast_charge_allowed_sensor_->publish_state(flags & FLAG_CHG);
  if (this->discharging_sensor_ != nullptr)
    this->discharging_sensor_->publish_state(flags & FLAG_DSG);
  if (this->battery_low_sensor_ != nullptr)
    this->battery_low_sensor_->publish_state(flags & FLAG_SOC1);
  if (this->battery_critical_sensor_ != nullptr)
    this->battery_critical_sensor_->publish_state(flags & FLAG_SOCF);
#endif
}

void SolderedBQ27441Component::dump_config() {
  ESP_LOGCONFIG(TAG, "Soldered BQ27441:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication failed");
  }
  ESP_LOGCONFIG(TAG, "  Design capacity: %u mAh", this->design_capacity_);
  ESP_LOGCONFIG(TAG, "  Design energy: %u mWh", this->design_energy_);
  ESP_LOGCONFIG(TAG, "  Terminate voltage: %u mV", this->terminate_voltage_);
  if (this->taper_rate_ != 0) {
    ESP_LOGCONFIG(TAG, "  Taper rate: %u (0.1 h)", this->taper_rate_);
  } else {
    ESP_LOGCONFIG(TAG, "  Taper rate: ROM default");
  }
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Voltage", this->voltage_sensor_);
  LOG_SENSOR("  ", "Current", this->current_sensor_);
  LOG_SENSOR("  ", "Power", this->power_sensor_);
  LOG_SENSOR("  ", "Battery Level", this->battery_level_sensor_);
  LOG_SENSOR("  ", "Remaining Capacity", this->remaining_capacity_sensor_);
  LOG_SENSOR("  ", "Full Capacity", this->full_capacity_sensor_);
  LOG_SENSOR("  ", "State of Health", this->state_of_health_sensor_);
  LOG_SENSOR("  ", "Temperature", this->temperature_sensor_);
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Fully Charged", this->fully_charged_sensor_);
  LOG_BINARY_SENSOR("  ", "Fast Charge Allowed", this->fast_charge_allowed_sensor_);
  LOG_BINARY_SENSOR("  ", "Discharging", this->discharging_sensor_);
  LOG_BINARY_SENSOR("  ", "Battery Low", this->battery_low_sensor_);
  LOG_BINARY_SENSOR("  ", "Battery Critical", this->battery_critical_sensor_);
#endif
}

// Standard/extended 2-byte command words are little-endian on the wire
bool SolderedBQ27441Component::read_word_(uint8_t reg, uint16_t *out) {
  uint8_t data[2];
  if (this->read_register(reg, data, sizeof(data)) != i2c::ERROR_OK)
    return false;
  *out = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  return true;
}

bool SolderedBQ27441Component::write_word_(uint8_t reg, uint16_t value) {
  uint8_t data[2] = {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)};
  return this->write_register(reg, data, sizeof(data)) == i2c::ERROR_OK;
}

// Writes a Control() subcommand and reads back its 2-byte result
bool SolderedBQ27441Component::read_control_word_(uint16_t function, uint16_t *out) {
  return this->write_word_(COMMAND_CONTROL, function) && this->read_word_(COMMAND_CONTROL, out);
}

// Writes a Control() subcommand that only triggers an action, no result to read back
bool SolderedBQ27441Component::execute_control_word_(uint16_t function) {
  return this->write_word_(COMMAND_CONTROL, function);
}

bool SolderedBQ27441Component::compute_block_checksum_(uint8_t *out_csum) {
  uint8_t data[32];
  if (this->read_register(EXTENDED_BLOCKDATA, data, sizeof(data)) != i2c::ERROR_OK)
    return false;
  uint8_t sum = 0;
  for (uint8_t byte : data)
    sum += byte;
  *out_csum = 255 - sum;
  return true;
}

// Writes up to 32 bytes to a data-memory subclass and updates its checksum. Config mode must already be open.
bool SolderedBQ27441Component::write_extended_data_(uint8_t class_id, uint8_t offset, const uint8_t *data,
                                                    uint8_t len) {
  const uint8_t enable = 0x00;
  const uint8_t block = offset / 32;
  if (this->write_register(EXTENDED_CONTROL, &enable, 1) != i2c::ERROR_OK ||
      this->write_register(EXTENDED_DATACLASS, &class_id, 1) != i2c::ERROR_OK ||
      this->write_register(EXTENDED_DATABLOCK, &block, 1) != i2c::ERROR_OK)
    return false;

  // The IC needs time to load the selected class/block into its RAM buffer before BlockData() reflects it
  delay(2);

  // Priming reads: the chip silently drops the first BlockData() byte write of a session unless BlockData is read at
  // least once right after selecting the class/offset. The values are discarded, only the read side effect matters.
  uint8_t priming_csum;
  if (!this->compute_block_checksum_(&priming_csum) ||
      this->read_register(EXTENDED_CHECKSUM, &priming_csum, 1) != i2c::ERROR_OK)
    return false;

  // One byte per transaction, not a burst - the BQ27441 NACKs multi-byte writes into the BlockData window even though
  // multi-byte reads from it work fine
  for (uint8_t i = 0; i < len; i++) {
    if (this->write_register(EXTENDED_BLOCKDATA + (offset % 32) + i, &data[i], 1) != i2c::ERROR_OK)
      return false;
  }

  // Let the last byte land in the RAM buffer before summing it, otherwise the checksum can cover a stale block and the
  // IC commits that instead
  delay(2);

  uint8_t csum;
  if (!this->compute_block_checksum_(&csum))
    return false;
  return this->write_register(EXTENDED_CHECKSUM, &csum, 1) == i2c::ERROR_OK;
}

bool SolderedBQ27441Component::write_extended_word_(uint8_t class_id, uint8_t offset, uint16_t value) {
  uint8_t data[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
  return this->write_extended_data_(class_id, offset, data, sizeof(data));
}

bool SolderedBQ27441Component::enter_config_() {
  uint16_t status;
  if (!this->read_control_word_(CONTROL_STATUS, &status))
    return false;
  this->sealed_on_entry_ = (status & STATUS_SS) != 0;

  // Datasheet requires the unseal key written twice in a row
  if (this->sealed_on_entry_ && !(this->execute_control_word_(UNSEAL_KEY) && this->execute_control_word_(UNSEAL_KEY)))
    return false;

  if (!this->execute_control_word_(CONTROL_SET_CFGUPDATE))
    return false;

  for (uint16_t i = 0; i < CONFIG_POLL_TRIES; i++) {
    delay(1);
    App.feed_wdt();
    uint16_t flags;
    if (!this->read_word_(COMMAND_FLAGS, &flags))
      return false;
    if (flags & FLAG_CFGUPMODE)
      return true;
  }
  ESP_LOGE(TAG, "Timed out entering config update mode");
  return false;
}

// Exits config mode via SOFT_RESET, which also clears ITPOR and resimulates SoC with the new model
bool SolderedBQ27441Component::exit_config_() {
  if (!this->execute_control_word_(CONTROL_SOFT_RESET))
    return false;

  bool exited = false;
  for (uint16_t i = 0; i < CONFIG_POLL_TRIES; i++) {
    uint16_t flags;
    if (!this->read_word_(COMMAND_FLAGS, &flags))
      return false;
    if (!(flags & FLAG_CFGUPMODE)) {
      exited = true;
      break;
    }
    delay(1);
    App.feed_wdt();
  }
  if (!exited) {
    ESP_LOGE(TAG, "Timed out exiting config update mode");
    return false;
  }

  if (this->sealed_on_entry_)
    return this->execute_control_word_(CONTROL_SEALED);
  return true;
}

// SOFT_RESET clears ITPOR even when a write before it failed, so a failed load is remembered in model_pending_ and
// retried on the next update
bool SolderedBQ27441Component::load_battery_model_() {
  this->model_pending_ = true;
  ESP_LOGI(TAG, "Loading battery model: %u mAh, %u mWh, terminate %u mV", this->design_capacity_, this->design_energy_,
           this->terminate_voltage_);

  if (!this->enter_config_()) {
    ESP_LOGE(TAG, "Failed to enter config update mode");
    return false;
  }

  bool ok = this->write_extended_word_(ID_STATE, STATE_DESIGN_CAPACITY, this->design_capacity_) &&
            this->write_extended_word_(ID_STATE, STATE_DESIGN_ENERGY, this->design_energy_) &&
            this->write_extended_word_(ID_STATE, STATE_TERMINATE_VOLTAGE, this->terminate_voltage_);
  if (ok && this->taper_rate_ != 0)
    ok = this->write_extended_word_(ID_STATE, STATE_TAPER_RATE, this->taper_rate_);
  if (!ok)
    ESP_LOGE(TAG, "Failed to write battery model");

  // Always leave config mode, even after a failed write, so the gauge resumes gauging
  bool exited = this->exit_config_();
  if (!exited)
    ESP_LOGE(TAG, "Failed to exit config update mode");
  // Read back what the gauge now reports, so a write that silently did not land shows up in the log
  uint16_t design_capacity;
  if (ok && exited) {
    if (!this->read_word_(COMMAND_DESIGN_CAPACITY, &design_capacity)) {
      ok = false;
    } else if (design_capacity == this->design_capacity_) {
      ESP_LOGI(TAG, "Battery model loaded");
    } else {
      ESP_LOGE(TAG, "Design capacity read back as %u mAh after loading %u mAh", design_capacity,
               this->design_capacity_);
      ok = false;
    }
  }
  if (ok && exited)
    this->model_pending_ = false;
  return !this->model_pending_;
}

}  // namespace soldered_bq27441
}  // namespace esphome
