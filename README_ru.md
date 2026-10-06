# Эмулятор Qingping CGG1 MiBeacon для ESPHome

[English version](README.md)

Эта ветка содержит ESPHome-компонент для эмуляции BLE advertising датчика
`Qingping Temp RH M` (CGG1-M). Предыдущий протокол Qingping `0xFDCD` здесь не
поддерживается.

## Поведение

После запуска компонент:

1. передаёт discovery-пакет Xiaomi MiBeacon `0xFE95` с frame control `0x5830`,
   product ID `0x0B48`, нативным публичным BLE MAC ESP32 и capability `0x08`;
2. по окончании `discovery_duration` переключается на незашифрованный
   measurement-пакет с frame control `0x5850`;
3. передаёт температуру и влажность в объекте `0x100D`;
4. добавляет объект батареи `0x100A` только при наличии `battery` или
   `battery_level`;
5. увеличивает MiBeacon frame counter при каждом обновлении measurement-пакета.

Scan response содержит оригинальное имя `Qingping Temp RH M`. Используются все
три legacy advertising-канала: 37, 38 и 39. GATT-сервис и BLE SMP pairing не
создаются. На уровне логирования `DEBUG` компонент выводит
полный advertising payload и scan response в hex, чтобы их можно было
побайтно сравнить с дампом.

Пакет `0x5830` воспроизводит обнаруженное состояние непривязанного CGG1-M.
Пакет `0x5850` следует открытому формату MiBeacon из проекта
[pvvx/ATC_MiThermometer](https://github.com/pvvx/ATC_MiThermometer).

## Подключение

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

`temperature` и `humidity` должны ссылаться на существующие сенсоры ESPHome.
Батарея необязательна и задаётся сенсором:

```yaml
qingping_cgg1:
  temperature: source_temperature
  humidity: source_humidity
  battery: source_battery
```

или постоянным значением:

```yaml
qingping_cgg1:
  temperature: source_temperature
  humidity: source_humidity
  battery_level: 90
```

Если ни один параметр батареи не указан, объект `0x100A` отсутствует в эфире.
Компонент не передаёт резервные значения: после discovery advertising с
измерениями запускается только при наличии конечных значений обязательных
сенсоров и останавливается при их недоступности.

## BLE MAC-адрес

По умолчанию компонент использует родной публичный BLE MAC ESP32. Для
эмуляции конкретного датчика нужно подменить только Bluetooth-адрес до
запуска `esp32_ble`:

```yaml
esphome:
  includes:
    - <esp_mac.h>
  on_boot:
    - priority: 400
      then:
        - lambda: |-
            // Обычный порядок отображения; не переворачивать байты.
            const uint8_t ble_mac[6] = {
              0x58, 0x2D, 0x34, 0x60, 0x78, 0x63
            };

            const esp_err_t err =
                esp_iface_mac_addr_set(ble_mac, ESP_MAC_BT);
            if (err == ESP_OK) {
              ESP_LOGI(
                "ble_mac",
                "BLE MAC set to %02X:%02X:%02X:%02X:%02X:%02X",
                ble_mac[0], ble_mac[1], ble_mac[2],
                ble_mac[3], ble_mac[4], ble_mac[5]
              );
            } else {
              ESP_LOGE(
                "ble_mac",
                "Failed to set BLE MAC: %s",
                esp_err_to_name(err)
              );
            }
```

Приоритет `400` выполняется до BLE-компонента ESPHome, у которого приоритет
инициализации `350`. Затем компонент считывает этот адрес и записывает его в
MiBeacon-пакет в требуемом протоколом порядке байтов. Подмена выполняется в
оперативной памяти при каждой загрузке: eFuse и Wi-Fi MAC не изменяются. Пока
эмулятор использует этот MAC, оригинальный датчик нужно держать выключенным.

## Параметры

- `temperature` — обязательный сенсор температуры;
- `humidity` — обязательный сенсор влажности;
- `battery` — необязательный сенсор заряда;
- `battery_level` — необязательное постоянное значение заряда, `0..100`;
  не может использоваться одновременно с `battery`;
- `discovery_duration` — время передачи `0x5830`, от 1 секунды до 10 минут,
  по умолчанию 30 секунд;
- `advertising_interval` — интервал от 20 мс до 1 секунды, по умолчанию
  1 секунда.

Не включайте `esp32_ble.advertising`, `esp32_ble_beacon` или
`esp32_ble_server`: компонент самостоятельно управляет raw BLE advertising.

Полный пример находится в `airmx-qingping-m5atom.yaml`.

## Что ещё не реализовано

Это экспериментальный незашифрованный профиль. Он не реализует оригинальные
зашифрованные кадры `0x5858`, получение bindkey или GATT-процедуру привязки.
Сначала следует проверить, принимает ли целевое устройство открытые объекты
`0x100D` и `0x100A`.
