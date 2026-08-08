# Эмулятор Qingping FDCD для ESPHome

[English version](README.md)

## Для чего это нужно

Этот внешний компонент ESPHome позволяет ESP32 представляться BLE-датчиком
Qingping Temp/RH Lite. Совместимый увлажнитель может получать температуру и
влажность из любого датчика ESPHome, в том числе из сущностей Home Assistant,
вместо физического датчика Qingping.

Увлажнитель пассивно считывает значения из BLE advertising.

## Проверено с

- AIRMX AirWater A3S
- TION Iris

Оба устройства успешно получают передаваемые значения температуры и
влажности. Компонент отправляет только проверенный пакет Qingping Service Data
`0xFDCD`.

## Как это работает

Компонент:

- использует нативный публичный BLE MAC-адрес ESP32 и записывает тот же адрес
  внутрь FDCD-пакета;
- включает каналы 37, 38 и 39 в каждый legacy advertising event;
- повторяет текущий пакет с настраиваемым интервалом — по умолчанию каждые
  100 мс, то есть номинально 10 раз в секунду на каждом канале; допустимый
  диапазон — от 20 мс до 1 секунды;
- принимает scan request от любого устройства, но разрешает BLE-соединения
  только устройствам из whitelist, чтобы соединение не прерывало advertising;
- не передаёт резервные или устаревшие измерения: advertising запускается
  только после получения конечных значений температуры и влажности,
  останавливается при недоступности любого из них и также требует актуальный
  заряд, если настроен параметр `battery`;
- обновляет пакет при изменении исходной температуры, влажности или
  необязательного датчика заряда;
- не создаёт GATT-сервис.

## Подключение

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

Параметры `temperature` и `humidity` должны ссылаться на идентификаторы любых
существующих датчиков ESPHome. Необязательный параметр `battery` может
ссылаться на отдельный датчик заряда; если он не задан, используется постоянное
значение `battery_level`.

Полный пример для M5Stack Atom находится в файле
`airmx-qingping-m5atom.yaml`. Значения можно импортировать из Home Assistant:

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

Не включайте `esp32_ble.advertising`, `esp32_ble_beacon` или
`esp32_ble_server`: компонент самостоятельно управляет legacy BLE advertising.

Сборка проверена с ESPHome 2026.7.3 и ESP-IDF 5.5.5 на M5Stack Atom
(ESP32-PICO-D4).
