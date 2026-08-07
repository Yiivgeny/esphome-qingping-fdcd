#include "qingping_fdcd.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <esp_bt_device.h>
#include <esp_err.h>

namespace esphome::qingping_fdcd {

static const char *const TAG = "qingping_fdcd";
static constexpr char DEVICE_NAME[] = "Qingping Temp RH Lite";

int16_t QingpingFDCD::encode_temperature_(float value) {
  const float scaled = std::round(value * 10.0f);
  return static_cast<int16_t>(std::clamp(
      scaled, static_cast<float>(std::numeric_limits<int16_t>::min()),
      static_cast<float>(std::numeric_limits<int16_t>::max())));
}

uint16_t QingpingFDCD::encode_humidity_(float value) {
  return static_cast<uint16_t>(std::round(std::clamp(value, 0.0f, 100.0f) * 10.0f));
}

uint8_t QingpingFDCD::encode_battery_(float value) {
  return static_cast<uint8_t>(std::round(std::clamp(value, 0.0f, 100.0f)));
}

void QingpingFDCD::setup() {
  this->advertising_params_.adv_type = ADV_TYPE_IND;
  this->advertising_params_.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  this->advertising_params_.channel_map = ADV_CHNL_ALL;
  this->advertising_params_.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

  this->temperature_sensor_->add_on_state_callback([this](float value) {
    if (!std::isfinite(value))
      return;
    const int16_t encoded = encode_temperature_(value);
    if (encoded != this->temperature_x10_) {
      this->temperature_x10_ = encoded;
      this->mark_payload_dirty_();
    }
  });

  this->humidity_sensor_->add_on_state_callback([this](float value) {
    if (!std::isfinite(value))
      return;
    const uint16_t encoded = encode_humidity_(value);
    if (encoded != this->humidity_x10_) {
      this->humidity_x10_ = encoded;
      this->mark_payload_dirty_();
    }
  });

  if (this->battery_sensor_ != nullptr) {
    this->battery_sensor_->add_on_state_callback([this](float value) {
      if (!std::isfinite(value))
        return;
      const uint8_t encoded = encode_battery_(value);
      if (encoded != this->battery_level_) {
        this->battery_level_ = encoded;
        this->mark_payload_dirty_();
      }
    });
  }
}

void QingpingFDCD::loop() {
  if (this->ble_parent_ == nullptr || !this->ble_parent_->is_active())
    return;

  if (!this->ble_was_active_) {
    this->ble_was_active_ = true;
    this->load_ble_address_();
  }
  if (!this->address_loaded_ || millis() < this->retry_after_ms_)
    return;

  if (this->payload_dirty_) {
    if (this->advertising_ || this->advertising_starting_) {
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

void QingpingFDCD::dump_config() {
  ESP_LOGCONFIG(TAG, "Qingping FDCD emulator:");
  ESP_LOGCONFIG(TAG, "  Advertising interval: %" PRIu32 " ms", this->advertising_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Advertising channels: 37, 38, 39");
  ESP_LOGCONFIG(TAG, "  Initial temperature: %.1f C", this->temperature_x10_ / 10.0f);
  ESP_LOGCONFIG(TAG, "  Initial humidity: %.1f %%", this->humidity_x10_ / 10.0f);
  ESP_LOGCONFIG(TAG, "  Battery: %u %%", this->battery_level_);
}

void QingpingFDCD::mark_payload_dirty_() { this->payload_dirty_ = true; }

void QingpingFDCD::load_ble_address_() {
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

void QingpingFDCD::build_payload_() {
  size_t i = 0;
  this->advertising_data_[i++] = 0x02;
  this->advertising_data_[i++] = 0x01;
  this->advertising_data_[i++] = 0x06;
  this->advertising_data_[i++] = 0x14;
  this->advertising_data_[i++] = 0x16;
  this->advertising_data_[i++] = 0xCD;
  this->advertising_data_[i++] = 0xFD;
  this->advertising_data_[i++] = 0x08;
  this->advertising_data_[i++] = 0x10;  // Qingping Temp/RH Lite product id.
  for (int8_t m = 5; m >= 0; --m)
    this->advertising_data_[i++] = this->mac_[m];
  this->advertising_data_[i++] = 0x01;
  this->advertising_data_[i++] = 0x04;
  this->advertising_data_[i++] = static_cast<uint8_t>(this->temperature_x10_ & 0xFF);
  this->advertising_data_[i++] = static_cast<uint8_t>((this->temperature_x10_ >> 8) & 0xFF);
  this->advertising_data_[i++] = static_cast<uint8_t>(this->humidity_x10_ & 0xFF);
  this->advertising_data_[i++] = static_cast<uint8_t>((this->humidity_x10_ >> 8) & 0xFF);
  this->advertising_data_[i++] = 0x02;
  this->advertising_data_[i++] = 0x01;
  this->advertising_data_[i++] = this->battery_level_;

  this->scan_response_[0] = sizeof(DEVICE_NAME);
  this->scan_response_[1] = 0x09;
  std::memcpy(this->scan_response_.data() + 2, DEVICE_NAME, sizeof(DEVICE_NAME) - 1);
}

void QingpingFDCD::configure_payload_() {
  this->build_payload_();
  this->payload_dirty_ = false;
  this->advertising_configured_ = false;
  this->scan_response_configured_ = false;

  this->advertising_configuring_ = true;
  esp_err_t err = esp_ble_gap_config_adv_data_raw(this->advertising_data_.data(), this->advertising_data_.size());
  if (err != ESP_OK) {
    this->advertising_configuring_ = false;
    this->payload_dirty_ = true;
    this->retry_after_ms_ = millis() + 1000;
    ESP_LOGE(TAG, "Could not configure advertising data: %s", esp_err_to_name(err));
  }

  this->scan_response_configuring_ = true;
  err = esp_ble_gap_config_scan_rsp_data_raw(this->scan_response_.data(), this->scan_response_.size());
  if (err != ESP_OK) {
    this->scan_response_configuring_ = false;
    this->payload_dirty_ = true;
    this->retry_after_ms_ = millis() + 1000;
    ESP_LOGE(TAG, "Could not configure scan response: %s", esp_err_to_name(err));
  }
}

void QingpingFDCD::start_advertising_() {
  const uint32_t interval_ms = std::clamp(this->advertising_interval_ms_, 20UL, 10240UL);
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

void QingpingFDCD::stop_advertising_() {
  if (this->advertising_stopping_)
    return;
  this->advertising_stopping_ = true;
  const esp_err_t err = esp_ble_gap_stop_advertising();
  if (err != ESP_OK) {
    this->advertising_stopping_ = false;
    this->advertising_ = false;
    this->advertising_starting_ = false;
    ESP_LOGW(TAG, "Could not stop advertising: %s", esp_err_to_name(err));
  }
}

void QingpingFDCD::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
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
      }
      break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
      this->advertising_stopping_ = false;
      this->advertising_starting_ = false;
      this->advertising_ = false;
      break;
    default:
      break;
  }
}

void QingpingFDCD::reset_ble_state_() {
  this->ble_was_active_ = false;
  this->address_loaded_ = false;
  this->payload_dirty_ = true;
  this->advertising_configuring_ = false;
  this->scan_response_configuring_ = false;
  this->advertising_configured_ = false;
  this->scan_response_configured_ = false;
  this->advertising_starting_ = false;
  this->advertising_stopping_ = false;
  this->advertising_ = false;
}

void QingpingFDCD::ble_before_disabled_event_handler() { this->reset_ble_state_(); }

}  // namespace esphome::qingping_fdcd
