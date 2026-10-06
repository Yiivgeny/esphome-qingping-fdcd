#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

#include <esp_gap_ble_api.h>

namespace esphome::qingping_cgg1 {

class QingpingCGG1 : public Component {
 public:
  void set_ble_parent(esp32_ble::ESP32BLE *parent) { this->ble_parent_ = parent; }
  void set_temperature_sensor(sensor::Sensor *sensor) { this->temperature_sensor_ = sensor; }
  void set_humidity_sensor(sensor::Sensor *sensor) { this->humidity_sensor_ = sensor; }
  void set_battery_sensor(sensor::Sensor *sensor) { this->battery_sensor_ = sensor; }
  void set_battery_level(uint8_t value) {
    this->battery_level_ = value;
    this->static_battery_configured_ = true;
  }
  void set_advertising_interval(uint32_t value) { this->advertising_interval_ms_ = value; }
  void set_discovery_duration(uint32_t value) { this->discovery_duration_ms_ = value; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
  void ble_before_disabled_event_handler();

 protected:
  enum class AdvertisementKind : uint8_t {
    DISCOVERY,
    MEASUREMENTS,
  };

  enum class AdvertisingStopReason : uint8_t {
    UNKNOWN,
    INPUT_UNAVAILABLE,
    PAYLOAD_UPDATE,
    DISCOVERY_COMPLETE,
  };

  static int16_t encode_temperature_(float value);
  static uint16_t encode_humidity_(float value);
  static uint8_t encode_battery_(float value);

  bool inputs_valid_() const;
  bool battery_included_() const;
  void update_temperature_(float value);
  void update_humidity_(float value);
  void update_battery_(float value);
  void log_input_transition_(bool was_valid);
  void mark_measurement_payload_dirty_();
  void load_ble_address_();
  void build_payload_();
  void build_discovery_payload_();
  void build_measurement_payload_();
  void build_scan_response_();
  void configure_payload_();
  void start_advertising_();
  void stop_advertising_();
  void reset_ble_state_();

  void append_u16_le_(size_t &index, uint16_t value);
  void append_mac_reversed_(size_t &index);

  esp32_ble::ESP32BLE *ble_parent_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *humidity_sensor_{nullptr};
  sensor::Sensor *battery_sensor_{nullptr};

  std::array<uint8_t, 6> mac_{};
  std::array<uint8_t, 31> advertising_data_{};
  std::array<uint8_t, 20> scan_response_{};
  size_t advertising_data_length_{0};
  size_t scan_response_length_{0};

  esp_ble_adv_params_t advertising_params_{};
  int16_t temperature_x10_{0};
  uint16_t humidity_x10_{0};
  uint32_t advertising_interval_ms_{1000};
  uint32_t discovery_duration_ms_{30000};
  uint32_t discovery_started_ms_{0};
  uint32_t retry_after_ms_{0};
  uint8_t battery_level_{0};
  uint8_t frame_counter_{0};

  bool ble_was_active_{false};
  bool address_loaded_{false};
  bool temperature_valid_{false};
  bool humidity_valid_{false};
  bool battery_valid_{false};
  bool static_battery_configured_{false};
  bool discovery_started_{false};
  bool discovery_complete_{false};
  bool payload_dirty_{true};
  bool advertising_configuring_{false};
  bool scan_response_configuring_{false};
  bool advertising_configured_{false};
  bool scan_response_configured_{false};
  bool advertising_starting_{false};
  bool advertising_stopping_{false};
  bool advertising_{false};
  AdvertisementKind active_advertisement_{AdvertisementKind::DISCOVERY};
  AdvertisingStopReason advertising_stop_reason_{AdvertisingStopReason::UNKNOWN};
};

}  // namespace esphome::qingping_cgg1
