# Qingping FDCD emulator for ESPHome

[Русская версия](README_ru.md)

## Purpose

This external ESPHome component makes an ESP32 advertise as a Qingping Temp/RH
Lite sensor. It allows a compatible humidifier to receive temperature and
humidity from any ESPHome sensor, including values imported from Home
Assistant, instead of using a physical Qingping sensor.

The humidifier reads the emulated sensor passively from BLE advertisements.

## Tested with

- AIRMX AirWater A3S
- TION Iris

Both devices successfully receive the advertised temperature and humidity.
The implementation sends only the tested Qingping `0xFDCD` Service Data frame.

## How it works

The component:

- uses the ESP32's native BLE public address and embeds the same address in the
  FDCD frame;
- includes channels 37, 38 and 39 in every legacy advertising event;
- repeats the current frame at a configurable interval (100 ms by default),
  nominally 10 times per second on every channel; accepted values range from
  20 ms to 1 second;
- accepts scan requests from any device but only permits BLE connections from
  whitelisted devices, preventing a connection from interrupting advertising;
- does not advertise fallback or stale measurements: advertising starts only
  after temperature and humidity have finite states, stops when either becomes
  unavailable, and also requires a valid battery state when `battery` is used;
- updates the frame whenever its source temperature, humidity, or optional
  battery sensor publishes a new value;
- exposes no GATT service.

## Installation

```yaml
external_components:
  - source: github://Yiivgeny/esphome-qingping-fdcd@main
    components: [qingping_fdcd]

esp32_ble:

qingping_fdcd:
  temperature: source_temperature
  humidity: source_humidity
  battery_level: 90
  advertising_interval: 100ms
```

`temperature` and `humidity` reference any existing ESPHome sensor IDs. The
optional `battery` setting can reference another sensor; otherwise the fixed
`battery_level` value is used.

## Using the original sensor MAC address

The ESP32 can use the BLE MAC address of the original sensor. Run the MAC
override at boot with priority `400`, before the BLE stack is initialized.
Replace the six bytes below with the original sensor address. Write them in
the normal displayed order; do not reverse them.

```yaml
esphome:
  includes:
    - <esp_mac.h>
  on_boot:
    priority: 400
    then:
      - lambda: |-
          // Write the MAC in normal order; do not reverse it.
          const uint8_t mac[6] = {
            0x10, 0x06, 0x1C, 0x15, 0xC5, 0xBA
          };

          const esp_err_t err =
              esp_iface_mac_addr_set(mac, ESP_MAC_BT);

          if (err == ESP_OK) {
            ESP_LOGI(
              "ble_mac",
              "BLE MAC set to %02X:%02X:%02X:%02X:%02X:%02X",
              mac[0], mac[1], mac[2],
              mac[3], mac[4], mac[5]
            );
          } else {
            ESP_LOGE(
              "ble_mac",
              "Failed to set BLE MAC: %s",
              esp_err_to_name(err)
            );
          }
```

Keep the original sensor powered off while the emulator is using its address.
Two active BLE devices with the same public MAC can cause unpredictable sensor
selection and packet attribution.

Start from `airmx-qingping-m5atom.yaml`. In a real configuration the sources
can be Home Assistant sensors:

```yaml
api:

sensor:
  - platform: homeassistant
    id: source_temperature
    entity_id: sensor.room_temperature
    internal: true

  - platform: homeassistant
    id: source_humidity
    entity_id: sensor.room_humidity
    internal: true
```

Do not enable `esp32_ble.advertising`, `esp32_ble_beacon`, or
`esp32_ble_server`: this component owns the legacy BLE advertising state.

The component has been build-tested with ESPHome 2026.7.3 and ESP-IDF 5.5.5 on
an M5Stack Atom (ESP32-PICO-D4).
