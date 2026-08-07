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
CONF_INITIAL_HUMIDITY = "initial_humidity"
CONF_INITIAL_TEMPERATURE = "initial_temperature"

qingping_fdcd_ns = cg.esphome_ns.namespace("qingping_fdcd")
QingpingFDCD = qingping_fdcd_ns.class_("QingpingFDCD", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(QingpingFDCD),
        cv.GenerateID(esp32_ble.CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
        cv.Required(CONF_TEMPERATURE): cv.use_id(sensor.Sensor),
        cv.Required(CONF_HUMIDITY): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_BATTERY): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_INITIAL_TEMPERATURE, default=23.0): cv.float_range(
            min=-3276.8, max=3276.7
        ),
        cv.Optional(CONF_INITIAL_HUMIDITY, default=50.0): cv.float_range(
            min=0.0, max=100.0
        ),
        cv.Optional(CONF_BATTERY_LEVEL, default=100): cv.int_range(min=0, max=100),
        cv.Optional(CONF_ADVERTISING_INTERVAL, default="1s"):
            cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


def _final_validate(config):
    ble_config = fv.full_config.get().get("esp32_ble", {})
    if ble_config.get("advertising", False):
        raise cv.Invalid(
            "qingping_fdcd owns BLE advertising; remove `advertising: true` "
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

    cg.add(var.set_initial_temperature(config[CONF_INITIAL_TEMPERATURE]))
    cg.add(var.set_initial_humidity(config[CONF_INITIAL_HUMIDITY]))
    cg.add(var.set_battery_level(config[CONF_BATTERY_LEVEL]))
    cg.add(
        var.set_advertising_interval(
            config[CONF_ADVERTISING_INTERVAL].total_milliseconds
        )
    )
