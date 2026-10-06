#include "qingping_cgg1.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <esp_bt_device.h>
#include <esp_err.h>

namespace esphome::qingping_cgg1 {

static const char *const TAG = "qingping_cgg1";
static constexpr char DEVICE_NAME[] = "Qingping Temp RH M";
static constexpr uint16_t MIBEACON_UUID = 0xFE95;
static constexpr uint16_t CGG1_PRODUCT_ID = 0x0B48;
static constexpr uint16_t DISCOVERY_FRAME_CONTROL = 0x5830;
static constexpr uint16_t MEASUREMENT_FRAME_CONTROL = 0x5850;
static constexpr uint16_t OBJECT_TEMPERATURE_HUMIDITY = 0x100D;
static constexpr uint16_t OBJECT_BATTERY = 0x100A;
static constexpr uint8_t CGG1_CAPABILITY = 0x08;

int16_t QingpingCGG1::encode_temperature_(float value) {
  const float scaled = std::round(value * 10.0f);
  return static_cast<int16_t>(std::clamp(
      scaled, static_cast<float>(std::numeric_limits<int16_t>::min()),
      static_cast<float>(std::numeric_limits<int16_t>::max())));
}

uint16_t QingpingCGG1::encode_humidity_(float value) {
  return static_cast<uint16_t>(std::round(std::clamp(value, 0.0f, 100.0f) * 10.0f));
}

uint8_t QingpingCGG1::encode_battery_(float value) {
  return static_cast<uint8_t>(std::round(std::clamp(value, 0.0f, 100.0f)));
}

void QingpingCGG1::setup() {
  this->advertising_params_.adv_type = ADV_TYPE_IND;
  this->advertising_params_.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  this->advertising_params_.channel_map = ADV_CHNL_ALL;
  this->advertising_params_.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_WLST;

  this->temperature_sensor_->add_on_state_callback([this](float value) { this->update_temperature_(value); });
  this->humidity_sensor_->add_on_state_callback([this](float value) { this->update_humidity_(value); });

  if (this->battery_sensor_ != nullptr) {
    this->battery_sensor_->add_on_state_callback([this](float value) { this->update_battery_(value); });
  }

  if (this->temperature_sensor_->has_state())
    this->update_temperature_(this->temperature_sensor_->state);
  if (this->humidity_sensor_->has_state())
    this->update_humidity_(this->humidity_sensor_->state);
  if (this->battery_sensor_ != nullptr && this->battery_sensor_->has_state())
    this->update_battery_(this->battery_sensor_->state);
}

void QingpingCGG1::loop() {
  if (this->ble_parent_ == nullptr || !this->ble_parent_->is_active())
    return;

  if (!this->ble_was_active_) {
    this->ble_was_active_ = true;
    this->load_ble_address_();
  }
  if (!this->address_loaded_ || millis() < this->retry_after_ms_)
    return;

  if (!this->discovery_started_) {
    this->discovery_started_ = true;
    this->discovery_started_ms_ = millis();
    this->payload_dirty_ = true;
    ESP_LOGW(TAG, "Discovery mode: MiBeacon 0x5830 for %" PRIu32 " ms", this->discovery_duration_ms_);
  }

  if (!this->discovery_complete_ && millis() - this->discovery_started_ms_ >= this->discovery_duration_ms_) {
    this->discovery_complete_ = true;
    this->payload_dirty_ = true;
    ESP_LOGW(TAG, "Discovery complete; switching to MiBeacon 0x5850 measurements");
    if (this->advertising_ || this->advertising_starting_) {
      this->advertising_stop_reason_ = AdvertisingStopReason::DISCOVERY_COMPLETE;
      this->stop_advertising_();
      return;
    }
  }

  if (this->discovery_complete_ && !this->inputs_valid_()) {
    if (this->advertising_ || this->advertising_starting_) {
      this->advertising_stop_reason_ = AdvertisingStopReason::INPUT_UNAVAILABLE;
      this->stop_advertising_();
    }
    return;
  }

  if (this->payload_dirty_) {
    if (this->advertising_ || this->advertising_starting_) {
      this->advertising_stop_reason_ = AdvertisingStopReason::PAYLOAD_UPDATE;
      this->stop_advertising_();
      return;
    }
    if (!this->advertising_stopping_ && !this->advertising_configuring_ &&
        !this->scan_response_configuring_) {
      this->configure_payload_();
    }
    return;
  }

  if (this->advertising_configured_ && this->scan_response_configured_ && !this->advertising_ &&
      !this->advertising_starting_ && !this->advertising_stopping_) {
    this->start_advertising_();
  }
}

void QingpingCGG1::dump_config() {
  ESP_LOGCONFIG(TAG, "Qingping CGG1 MiBeacon emulator:");
  ESP_LOGCONFIG(TAG, "  Product ID: 0x%04X", CGG1_PRODUCT_ID);
  ESP_LOGCONFIG(TAG, "  Discovery frame control: 0x%04X", DISCOVERY_FRAME_CONTROL);
  ESP_LOGCONFIG(TAG, "  Measurement frame control: 0x%04X", MEASUREMENT_FRAME_CONTROL);
  ESP_LOGCONFIG(TAG, "  Discovery duration: %" PRIu32 " ms", this->discovery_duration_ms_);
  ESP_LOGCONFIG(TAG, "  Advertising interval: %" PRIu32 " ms", this->advertising_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Advertising channels: 37, 38, 39");
  ESP_LOGCONFIG(TAG, "  Connection requests: whitelist only");
  ESP_LOGCONFIG(TAG, "  Temperature input: %s", this->temperature_valid_ ? "ready" : "waiting");
  ESP_LOGCONFIG(TAG, "  Humidity input: %s", this->humidity_valid_ ? "ready" : "waiting");
  if (this->battery_sensor_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Battery input: %s", this->battery_valid_ ? "ready" : "waiting");
  else if (this->static_battery_configured_)
    ESP_LOGCONFIG(TAG, "  Static battery: %u %%", this->battery_level_);
  else
    ESP_LOGCONFIG(TAG, "  Battery object: omitted");
}

bool QingpingCGG1::inputs_valid_() const {
  return this->temperature_valid_ && this->humidity_valid_ &&
         (this->battery_sensor_ == nullptr || this->battery_valid_);
}

bool QingpingCGG1::battery_included_() const {
  return this->battery_sensor_ != nullptr || this->static_battery_configured_;
}

void QingpingCGG1::log_input_transition_(bool was_valid) {
  const bool is_valid = this->inputs_valid_();
  if (was_valid == is_valid)
    return;
  if (is_valid) {
    ESP_LOGW(TAG, "Measurement inputs ready: temperature=%.1f C, humidity=%.1f %%%s",
             this->temperature_x10_ / 10.0f, this->humidity_x10_ / 10.0f,
             this->discovery_complete_ ? "" : "; waiting for discovery window");
  } else {
    ESP_LOGW(TAG, "Measurement input unavailable (temperature=%s, humidity=%s, battery=%s)",
             this->temperature_valid_ ? "ready" : "unavailable", this->humidity_valid_ ? "ready" : "unavailable",
             this->battery_sensor_ == nullptr ? "not required" : (this->battery_valid_ ? "ready" : "unavailable"));
  }
}

void QingpingCGG1::update_temperature_(float value) {
  const bool inputs_were_valid = this->inputs_valid_();
  const bool value_is_valid = std::isfinite(value);
  if (value_is_valid) {
    const int16_t encoded = encode_temperature_(value);
    if (!this->temperature_valid_ || encoded != this->temperature_x10_) {
      this->temperature_x10_ = encoded;
      this->temperature_valid_ = true;
      this->mark_measurement_payload_dirty_();
    }
  } else if (this->temperature_valid_) {
    this->temperature_valid_ = false;
    this->mark_measurement_payload_dirty_();
  }
  this->log_input_transition_(inputs_were_valid);
}

void QingpingCGG1::update_humidity_(float value) {
  const bool inputs_were_valid = this->inputs_valid_();
  const bool value_is_valid = std::isfinite(value);
  if (value_is_valid) {
    const uint16_t encoded = encode_humidity_(value);
    if (!this->humidity_valid_ || encoded != this->humidity_x10_) {
      this->humidity_x10_ = encoded;
      this->humidity_valid_ = true;
      this->mark_measurement_payload_dirty_();
    }
  } else if (this->humidity_valid_) {
    this->humidity_valid_ = false;
    this->mark_measurement_payload_dirty_();
  }
  this->log_input_transition_(inputs_were_valid);
}

void QingpingCGG1::update_battery_(float value) {
  const bool inputs_were_valid = this->inputs_valid_();
  const bool value_is_valid = std::isfinite(value);
  if (value_is_valid) {
    const uint8_t encoded = encode_battery_(value);
    if (!this->battery_valid_ || encoded != this->battery_level_) {
      this->battery_level_ = encoded;
      this->battery_valid_ = true;
      this->mark_measurement_payload_dirty_();
    }
  } else if (this->battery_valid_) {
    this->battery_valid_ = false;
    this->mark_measurement_payload_dirty_();
  }
  this->log_input_transition_(inputs_were_valid);
}

void QingpingCGG1::mark_measurement_payload_dirty_() {
  if (this->discovery_complete_)
    this->payload_dirty_ = true;
}

void QingpingCGG1::load_ble_address_() {
  const uint8_t *address = esp_bt_dev_get_address();
  if (address == nullptr) {
    this->retry_after_ms_ = millis() + 500;
    return;
  }
  std::copy_n(address, this->mac_.size(), this->mac_.begin());
  this->address_loaded_ = true;
  this->payload_dirty_ = true;
  ESP_LOGI(TAG, "Using native BLE MAC %02X:%02X:%02X:%02X:%02X:%02X", this->mac_[0], this->mac_[1],
           this->mac_[2], this->mac_[3], this->mac_[4], this->mac_[5]);
}

void QingpingCGG1::append_u16_le_(size_t &index, uint16_t value) {
  this->advertising_data_[index++] = static_cast<uint8_t>(value & 0xFF);
  this->advertising_data_[index++] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void QingpingCGG1::append_mac_reversed_(size_t &index) {
  for (int8_t m = 5; m >= 0; --m)
    this->advertising_data_[index++] = this->mac_[m];
}

void QingpingCGG1::build_discovery_payload_() {
  size_t i = 0;
  this->advertising_data_[i++] = 0x02;
  this->advertising_data_[i++] = 0x01;
  this->advertising_data_[i++] = 0x06;
  const size_t ad_length_index = i++;
  this->advertising_data_[i++] = 0x16;
  this->append_u16_le_(i, MIBEACON_UUID);
  this->append_u16_le_(i, DISCOVERY_FRAME_CONTROL);
  this->append_u16_le_(i, CGG1_PRODUCT_ID);
  this->advertising_data_[i++] = this->frame_counter_;
  this->append_mac_reversed_(i);
  this->advertising_data_[i++] = CGG1_CAPABILITY;
  this->advertising_data_[ad_length_index] = static_cast<uint8_t>(i - ad_length_index - 1);
  this->advertising_data_length_ = i;
  this->active_advertisement_ = AdvertisementKind::DISCOVERY;
}

void QingpingCGG1::build_measurement_payload_() {
  size_t i = 0;
  this->advertising_data_[i++] = 0x02;
  this->advertising_data_[i++] = 0x01;
  this->advertising_data_[i++] = 0x06;
  const size_t ad_length_index = i++;
  this->advertising_data_[i++] = 0x16;
  this->append_u16_le_(i, MIBEACON_UUID);
  this->append_u16_le_(i, MEASUREMENT_FRAME_CONTROL);
  this->append_u16_le_(i, CGG1_PRODUCT_ID);
  this->advertising_data_[i++] = ++this->frame_counter_;
  this->append_mac_reversed_(i);

  this->append_u16_le_(i, OBJECT_TEMPERATURE_HUMIDITY);
  this->advertising_data_[i++] = 0x04;
  this->append_u16_le_(i, static_cast<uint16_t>(this->temperature_x10_));
  this->append_u16_le_(i, this->humidity_x10_);

  if (this->battery_included_()) {
    this->append_u16_le_(i, OBJECT_BATTERY);
    this->advertising_data_[i++] = 0x01;
    this->advertising_data_[i++] = this->battery_level_;
  }

  this->advertising_data_[ad_length_index] = static_cast<uint8_t>(i - ad_length_index - 1);
  this->advertising_data_length_ = i;
  this->active_advertisement_ = AdvertisementKind::MEASUREMENTS;
}

void QingpingCGG1::build_scan_response_() {
  this->scan_response_[0] = sizeof(DEVICE_NAME);
  this->scan_response_[1] = 0x09;
  std::memcpy(this->scan_response_.data() + 2, DEVICE_NAME, sizeof(DEVICE_NAME) - 1);
  this->scan_response_length_ = sizeof(DEVICE_NAME) + 1;
}

void QingpingCGG1::build_payload_() {
  if (this->discovery_complete_)
    this->build_measurement_payload_();
  else
    this->build_discovery_payload_();
  this->build_scan_response_();
}

void QingpingCGG1::configure_payload_() {
  this->build_payload_();
  this->payload_dirty_ = false;
  this->advertising_configured_ = false;
  this->scan_response_configured_ = false;

  char advertising_hex[format_hex_pretty_size(31)];
  char scan_response_hex[format_hex_pretty_size(20)];
  ESP_LOGD(TAG, "Advertising payload (%u bytes): %s", static_cast<unsigned>(this->advertising_data_length_),
           format_hex_pretty_to(advertising_hex, this->advertising_data_.data(), this->advertising_data_length_));
  ESP_LOGD(TAG, "Scan response (%u bytes): %s", static_cast<unsigned>(this->scan_response_length_),
           format_hex_pretty_to(scan_response_hex, this->scan_response_.data(), this->scan_response_length_));

  this->advertising_configuring_ = true;
  esp_err_t err = esp_ble_gap_config_adv_data_raw(this->advertising_data_.data(), this->advertising_data_length_);
  if (err != ESP_OK) {
    this->advertising_configuring_ = false;
    this->payload_dirty_ = true;
    this->retry_after_ms_ = millis() + 1000;
    ESP_LOGE(TAG, "Could not configure advertising data: %s", esp_err_to_name(err));
  }

  this->scan_response_configuring_ = true;
  err = esp_ble_gap_config_scan_rsp_data_raw(this->scan_response_.data(), this->scan_response_length_);
  if (err != ESP_OK) {
    this->scan_response_configuring_ = false;
    this->payload_dirty_ = true;
    this->retry_after_ms_ = millis() + 1000;
    ESP_LOGE(TAG, "Could not configure scan response: %s", esp_err_to_name(err));
  }
}

void QingpingCGG1::start_advertising_() {
  const uint32_t interval_ms = std::clamp(this->advertising_interval_ms_, 20UL, 1000UL);
  const uint16_t interval_units = static_cast<uint16_t>((interval_ms * 8U + 2U) / 5U);
  this->advertising_params_.adv_int_min = interval_units;
  this->advertising_params_.adv_int_max = interval_units;
  this->advertising_starting_ = true;

  const esp_err_t err = esp_ble_gap_start_advertising(&this->advertising_params_);
  if (err != ESP_OK) {
    this->advertising_starting_ = false;
    this->retry_after_ms_ = millis() + 1000;
    ESP_LOGE(TAG, "Could not start advertising: %s", esp_err_to_name(err));
  }
}

void QingpingCGG1::stop_advertising_() {
  if (this->advertising_stopping_)
    return;
  this->advertising_stopping_ = true;
  const esp_err_t err = esp_ble_gap_stop_advertising();
  if (err != ESP_OK) {
    this->advertising_stopping_ = false;
    this->advertising_ = false;
    this->advertising_starting_ = false;
    this->advertising_stop_reason_ = AdvertisingStopReason::UNKNOWN;
    ESP_LOGW(TAG, "Could not stop advertising: %s", esp_err_to_name(err));
  }
}

void QingpingCGG1::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
      this->advertising_configuring_ = false;
      this->advertising_configured_ = param->adv_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS;
      if (!this->advertising_configured_) {
        this->payload_dirty_ = true;
        ESP_LOGE(TAG, "Advertising data rejected, status=%d", param->adv_data_raw_cmpl.status);
      }
      break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
      this->scan_response_configuring_ = false;
      this->scan_response_configured_ = param->scan_rsp_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS;
      if (!this->scan_response_configured_) {
        this->payload_dirty_ = true;
        ESP_LOGE(TAG, "Scan response rejected, status=%d", param->scan_rsp_data_raw_cmpl.status);
      }
      break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      this->advertising_starting_ = false;
      this->advertising_ = param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
      if (!this->advertising_) {
        this->retry_after_ms_ = millis() + 1000;
        ESP_LOGE(TAG, "Advertising start failed, status=%d", param->adv_start_cmpl.status);
      } else if (this->active_advertisement_ == AdvertisementKind::DISCOVERY) {
        ESP_LOGW(TAG, "Advertising ON: discovery 0x5830, product=0x%04X, counter=%u", CGG1_PRODUCT_ID,
                 this->frame_counter_);
      } else if (this->battery_included_()) {
        ESP_LOGW(TAG,
                 "Advertising ON: measurements 0x5850, temperature=%.1f C, humidity=%.1f %%, battery=%u %%",
                 this->temperature_x10_ / 10.0f, this->humidity_x10_ / 10.0f, this->battery_level_);
      } else {
        ESP_LOGW(TAG, "Advertising ON: measurements 0x5850, temperature=%.1f C, humidity=%.1f %%, battery=omitted",
                 this->temperature_x10_ / 10.0f, this->humidity_x10_ / 10.0f);
      }
      break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
      this->advertising_stopping_ = false;
      this->advertising_starting_ = false;
      this->advertising_ = false;
      if (this->advertising_stop_reason_ == AdvertisingStopReason::INPUT_UNAVAILABLE) {
        ESP_LOGW(TAG, "Advertising OFF: required measurement input is unavailable");
      } else if (this->advertising_stop_reason_ == AdvertisingStopReason::PAYLOAD_UPDATE) {
        ESP_LOGW(TAG, "Advertising OFF: applying MiBeacon payload update");
      } else if (this->advertising_stop_reason_ == AdvertisingStopReason::DISCOVERY_COMPLETE) {
        ESP_LOGW(TAG, "Advertising OFF: discovery window complete");
      } else {
        ESP_LOGW(TAG, "Advertising OFF");
      }
      this->advertising_stop_reason_ = AdvertisingStopReason::UNKNOWN;
      break;
    default:
      break;
  }
}

void QingpingCGG1::reset_ble_state_() {
  this->ble_was_active_ = false;
  this->address_loaded_ = false;
  this->discovery_started_ = false;
  this->discovery_complete_ = false;
  this->frame_counter_ = 0;
  this->payload_dirty_ = true;
  this->advertising_configuring_ = false;
  this->scan_response_configuring_ = false;
  this->advertising_configured_ = false;
  this->scan_response_configured_ = false;
  this->advertising_starting_ = false;
  this->advertising_stopping_ = false;
  this->advertising_ = false;
  this->active_advertisement_ = AdvertisementKind::DISCOVERY;
  this->advertising_stop_reason_ = AdvertisingStopReason::UNKNOWN;
}

void QingpingCGG1::ble_before_disabled_event_handler() { this->reset_ble_state_(); }

}  // namespace esphome::qingping_cgg1
