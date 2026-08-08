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
