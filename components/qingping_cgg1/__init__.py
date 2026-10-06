import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import esp32_ble, sensor
from esphome.const import CONF_HUMIDITY, CONF_ID, CONF_TEMPERATURE


DEPENDENCIES = ["esp32_ble"]
CODEOWNERS = []

CONF_ADVERTISING_INTERVAL = "advertising_interval"
CONF_BATTERY = "battery"
CONF_BATTERY_LEVEL = "battery_level"
CONF_DISCOVERY_DURATION = "discovery_duration"

MIN_ADVERTISING_INTERVAL_MS = 20
MAX_ADVERTISING_INTERVAL_MS = 1000
MIN_DISCOVERY_DURATION_MS = 1000
MAX_DISCOVERY_DURATION_MS = 600000

qingping_cgg1_ns = cg.esphome_ns.namespace("qingping_cgg1")
QingpingCGG1 = qingping_cgg1_ns.class_("QingpingCGG1", cg.Component)


def _validate_advertising_interval(value):
    interval = cv.positive_time_period_milliseconds(value)
    if not (
        MIN_ADVERTISING_INTERVAL_MS
        <= interval.total_milliseconds
        <= MAX_ADVERTISING_INTERVAL_MS
    ):
        raise cv.Invalid(
            "advertising_interval must be between 20ms and 1000ms"
        )
    return interval


def _validate_discovery_duration(value):
    duration = cv.positive_time_period_milliseconds(value)
    if not (
        MIN_DISCOVERY_DURATION_MS
        <= duration.total_milliseconds
        <= MAX_DISCOVERY_DURATION_MS
    ):
        raise cv.Invalid(
            "discovery_duration must be between 1s and 10min"
        )
    return duration


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(QingpingCGG1),
        cv.GenerateID(esp32_ble.CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
        cv.Required(CONF_TEMPERATURE): cv.use_id(sensor.Sensor),
        cv.Required(CONF_HUMIDITY): cv.use_id(sensor.Sensor),
        cv.Exclusive(CONF_BATTERY, "battery_source"): cv.use_id(sensor.Sensor),
        cv.Exclusive(CONF_BATTERY_LEVEL, "battery_source"): cv.int_range(
            min=0, max=100
        ),
        cv.Optional(CONF_ADVERTISING_INTERVAL, default="1s"):
            _validate_advertising_interval,
        cv.Optional(CONF_DISCOVERY_DURATION, default="30s"):
            _validate_discovery_duration,
    }
).extend(cv.COMPONENT_SCHEMA)


def _final_validate(config):
    ble_config = fv.full_config.get().get("esp32_ble", {})
    if ble_config.get("advertising", False):
        raise cv.Invalid(
            "qingping_cgg1 owns BLE advertising; remove `advertising: true` "
            "from `esp32_ble:`"
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    ble = await cg.get_variable(config[esp32_ble.CONF_BLE_ID])
    cg.add(var.set_ble_parent(ble))
    esp32_ble.register_gap_event_handler(ble, var)
    esp32_ble.register_ble_status_event_handler(ble, var)

    temperature = await cg.get_variable(config[CONF_TEMPERATURE])
    humidity = await cg.get_variable(config[CONF_HUMIDITY])
    cg.add(var.set_temperature_sensor(temperature))
    cg.add(var.set_humidity_sensor(humidity))

    if CONF_BATTERY in config:
        battery = await cg.get_variable(config[CONF_BATTERY])
        cg.add(var.set_battery_sensor(battery))
    if CONF_BATTERY_LEVEL in config:
        cg.add(var.set_battery_level(config[CONF_BATTERY_LEVEL]))

    cg.add(
        var.set_advertising_interval(
            config[CONF_ADVERTISING_INTERVAL].total_milliseconds
        )
    )
    cg.add(
        var.set_discovery_duration(
            config[CONF_DISCOVERY_DURATION].total_milliseconds
        )
    )
