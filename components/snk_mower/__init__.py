import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_BATTERY_LEVEL,
    CONF_BATTERY_VOLTAGE,
)
from esphome.components import uart, sensor, binary_sensor, text_sensor

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["text_sensor", "json", "wifi"]

snk_mower_ns = cg.esphome_ns.namespace("snk_mower")
SnkMower = snk_mower_ns.class_("SnkMower", cg.Component, uart.UARTDevice)

CONF_PIN = "pin"
CONF_DISPLAY_CLK = "display_clk"
CONF_DISPLAY_MOSI = "display_mosi"
CONF_DISPLAY_CS = "display_cs"
CONF_BUZZER_PIN = "buzzer_pin"
CONF_DISPLAY_OFF_TIMEOUT = "display_off_timeout"
CONF_RAIN_PIN = "rain_pin"

CONF_ERROR_CODE = "error_code"
CONF_IS_MOWING = "is_mowing"
CONF_IS_CHARGING = "is_charging"
CONF_IS_DOCKED = "is_docked"
CONF_HAS_ERROR = "has_error"
CONF_IS_LOCKED = "is_locked"
CONF_IS_RETURNING = "is_returning"

CONF_LIGHT_LEVEL = "light_level"
CONF_SIGNAL_LEVEL = "signal_level"
CONF_WORK_AREA = "work_area"
CONF_CUT_AREA = "cut_area"
CONF_TOTAL_MINUTES = "total_minutes"
CONF_ON_MINUTES = "on_minutes"
CONF_BAT_HEALTH = "bat_health"
CONF_BAT_LEVEL_BARS = "bat_level_bars"
CONF_RAIN_DELAY = "rain_delay"

CONF_DEVICE_NAME = "device_name"
CONF_MODEL = "model"
CONF_SERIAL = "serial"
CONF_FIRMWARE_VERSION = "firmware_version"
CONF_BATTERY_NAME = "battery_name"
CONF_MOWER_STATE = "mower_state"


def validate_pin(value):
    value = cv.string(value)
    if len(value) != 4 or not value.isdigit():
        raise cv.Invalid("PIN must be exactly 4 digits")
    return value


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SnkMower),
            cv.Optional(CONF_PIN, default="9633"): validate_pin,
            cv.Optional(CONF_DISPLAY_CLK, default=18): cv.int_range(0, 39),
            cv.Optional(CONF_DISPLAY_MOSI, default=23): cv.int_range(0, 39),
            cv.Optional(CONF_DISPLAY_CS, default=5): cv.int_range(0, 39),
            cv.Optional(CONF_BUZZER_PIN): cv.int_range(0, 39),
            cv.Optional(CONF_DISPLAY_OFF_TIMEOUT, default=0): cv.positive_int,
            cv.Optional(CONF_RAIN_PIN): cv.int_range(0, 39),
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement="%",
                accuracy_decimals=0,
                device_class="battery",
            ),
            cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
                unit_of_measurement="V",
                accuracy_decimals=2,
                device_class="voltage",
                state_class="measurement",
            ),
            cv.Optional(CONF_ERROR_CODE): sensor.sensor_schema(
                icon="mdi:alert-circle",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_LIGHT_LEVEL): sensor.sensor_schema(
                icon="mdi:brightness-5",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_SIGNAL_LEVEL): sensor.sensor_schema(
                icon="mdi:signal",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_WORK_AREA): sensor.sensor_schema(
                unit_of_measurement="m²",
                icon="mdi:map-marker-area",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_CUT_AREA): sensor.sensor_schema(
                unit_of_measurement="m²",
                icon="mdi:grass",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_TOTAL_MINUTES): sensor.sensor_schema(
                unit_of_measurement="min",
                icon="mdi:clock-outline",
                accuracy_decimals=0,
                state_class="total_increasing",
            ),
            cv.Optional(CONF_ON_MINUTES): sensor.sensor_schema(
                unit_of_measurement="min",
                icon="mdi:timer-outline",
                accuracy_decimals=0,
                state_class="total_increasing",
            ),
            cv.Optional(CONF_BAT_HEALTH): sensor.sensor_schema(
                unit_of_measurement="%",
                icon="mdi:heart-pulse",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_BAT_LEVEL_BARS): sensor.sensor_schema(
                icon="mdi:battery",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_RAIN_DELAY): sensor.sensor_schema(
                unit_of_measurement="min",
                icon="mdi:weather-rainy",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_IS_MOWING): binary_sensor.binary_sensor_schema(
                device_class="running",
            ),
            cv.Optional(CONF_IS_CHARGING): binary_sensor.binary_sensor_schema(
                device_class="plug",
            ),
            cv.Optional(CONF_IS_DOCKED): binary_sensor.binary_sensor_schema(
                device_class="connectivity",
            ),
            cv.Optional(CONF_HAS_ERROR): binary_sensor.binary_sensor_schema(
                device_class="problem",
            ),
            cv.Optional(CONF_IS_LOCKED): binary_sensor.binary_sensor_schema(
                icon="mdi:lock",
            ),
            cv.Optional(CONF_IS_RETURNING): binary_sensor.binary_sensor_schema(
                icon="mdi:home-import-outline",
            ),
            cv.Optional(CONF_DEVICE_NAME): text_sensor.text_sensor_schema(
                icon="mdi:label",
            ),
            cv.Optional(CONF_MODEL): text_sensor.text_sensor_schema(
                icon="mdi:information",
            ),
            cv.Optional(CONF_SERIAL): text_sensor.text_sensor_schema(
                icon="mdi:barcode",
            ),
            cv.Optional(CONF_FIRMWARE_VERSION): text_sensor.text_sensor_schema(
                icon="mdi:package-up",
            ),
            cv.Optional(CONF_BATTERY_NAME): text_sensor.text_sensor_schema(
                icon="mdi:battery-info",
            ),
            cv.Optional(CONF_MOWER_STATE): text_sensor.text_sensor_schema(
                icon="mdi:state-machine",
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(
        config[CONF_ID],
        cg.RawExpression(f'"{config[CONF_PIN]}"'),
    )
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_display_pins(
        config[CONF_DISPLAY_CLK],
        config[CONF_DISPLAY_MOSI],
        config[CONF_DISPLAY_CS],
    ))

    if CONF_BUZZER_PIN in config:
        cg.add(var.set_buzzer_pin(config[CONF_BUZZER_PIN]))

    if CONF_RAIN_PIN in config:
        cg.add(var.set_rain_pin(config[CONF_RAIN_PIN]))

    if config[CONF_DISPLAY_OFF_TIMEOUT] > 0:
        cg.add(var.set_display_off_timeout(config[CONF_DISPLAY_OFF_TIMEOUT]))

    for key, setter in [
        (CONF_BATTERY_LEVEL, "set_battery_level_sensor"),
        (CONF_BATTERY_VOLTAGE, "set_battery_voltage_sensor"),
        (CONF_ERROR_CODE, "set_error_code_sensor"),
        (CONF_LIGHT_LEVEL, "set_light_level_sensor"),
        (CONF_SIGNAL_LEVEL, "set_signal_level_sensor"),
        (CONF_WORK_AREA, "set_work_area_sensor"),
        (CONF_CUT_AREA, "set_cut_area_sensor"),
        (CONF_TOTAL_MINUTES, "set_total_minutes_sensor"),
        (CONF_ON_MINUTES, "set_on_minutes_sensor"),
        (CONF_BAT_HEALTH, "set_bat_health_sensor"),
        (CONF_BAT_LEVEL_BARS, "set_bat_level_bars_sensor"),
        (CONF_RAIN_DELAY, "set_rain_delay_sensor"),
    ]:
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter in [
        (CONF_IS_MOWING, "set_is_mowing_sensor"),
        (CONF_IS_CHARGING, "set_is_charging_sensor"),
        (CONF_IS_DOCKED, "set_is_docked_sensor"),
        (CONF_HAS_ERROR, "set_has_error_sensor"),
        (CONF_IS_LOCKED, "set_is_locked_sensor"),
        (CONF_IS_RETURNING, "set_is_returning_sensor"),
    ]:
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter in [
        (CONF_DEVICE_NAME, "set_device_name_sensor"),
        (CONF_MODEL, "set_model_sensor"),
        (CONF_SERIAL, "set_serial_sensor"),
        (CONF_FIRMWARE_VERSION, "set_firmware_version_sensor"),
        (CONF_BATTERY_NAME, "set_battery_name_sensor"),
        (CONF_MOWER_STATE, "set_mower_state_sensor"),
    ]:
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(var, setter)(sens))
