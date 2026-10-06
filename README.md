# Qingping CGG1 MiBeacon emulator for ESPHome

[Русская версия](README_ru.md)

This branch provides an ESPHome component that emulates the BLE advertising of
the `Qingping Temp RH M` (CGG1-M). The previous Qingping `0xFDCD` protocol is not
supported in this branch.

## Behaviour

After boot the component:

1. advertises a Xiaomi MiBeacon `0xFE95` discovery frame with frame control
   `0x5830`, product ID `0x0B48`, the ESP32 native public BLE MAC, and capability
   byte `0x08`;
2. switches to an unencrypted `0x5850` measurement frame after
   `discovery_duration`;
3. sends temperature and humidity in MiBeacon object `0x100D`;
4. includes battery object `0x100A` only when `battery` or `battery_level` is
   configured;
5. increments the MiBeacon frame counter whenever the measurement payload is
   rebuilt.

The scan response contains the original `Qingping Temp RH M` name. Legacy
advertising uses channels 37, 38, and 39. No GATT service or BLE SMP pairing is
implemented. At `DEBUG` log level the component prints the complete advertising
payload and scan response in hexadecimal, so a capture can be compared byte for
byte.

The `0x5830` packet reproduces the observed unbound CGG1-M discovery state. The
`0x5850` packet follows the open MiBeacon format documented by
[pvvx/ATC_MiThermometer](https://github.com/pvvx/ATC_MiThermometer).

## Configuration

```yaml
external_components:
  - source: github://Yiivgeny/esphome-qingping-fdcd@mibeacon-cgg1
    components: [qingping_cgg1]

esp32_ble:

qingping_cgg1:
  temperature: source_temperature
  humidity: source_humidity
  discovery_duration: 30s
  advertising_interval: 1s
```

`temperature` and `humidity` reference existing ESPHome sensors. Battery is
optional and can be supplied by a sensor:

```yaml
qingping_cgg1:
  temperature: source_temperature
  humidity: source_humidity
  battery: source_battery
```

or as a static value:

```yaml
qingping_cgg1:
  temperature: source_temperature
  humidity: source_humidity
  battery_level: 90
```

When neither battery option is present, object `0x100A` is omitted. The
component does not transmit fallback measurements: after discovery it starts
measurement advertising only when all required inputs have finite values and
stops when one becomes unavailable.

## Options

- `temperature` — required temperature sensor;
- `humidity` — required humidity sensor;
- `battery` — optional battery sensor;
- `battery_level` — optional static battery level, `0..100`; it cannot be used
  together with `battery`;
- `discovery_duration` — `0x5830` phase duration from 1 second to 10 minutes,
  30 seconds by default;
- `advertising_interval` — 20 ms to 1 second, 1 second by default.

Do not enable `esp32_ble.advertising`, `esp32_ble_beacon`, or
`esp32_ble_server`; this component owns raw BLE advertising.

See `airmx-qingping-m5atom.yaml` for a complete example.

## Not implemented

This is an experimental plaintext profile. Original encrypted `0x5858` frames,
bindkey provisioning, and the CGG1 GATT binding procedure are not implemented.
The first test is whether the target accepts plaintext `0x100D` and `0x100A`
objects.
