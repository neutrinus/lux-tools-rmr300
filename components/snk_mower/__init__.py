import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_BATTERY_LEVEL,
    CONF_BATTERY_VOLTAGE,
)
from esphome.components import uart, sensor, text_sensor, binary_sensor

DEPENDENCIES = ["uart", "spi"]

snk_mower_ns = cg.esphome_ns.namespace("snk_mower")
SnkMower = snk_mower_ns.class_("SnkMower", cg.Component, uart.UARTDevice)

CONF_PIN = "pin"
CONF_DISPLAY_CLK = "display_clk"
CONF_DISPLAY_MOSI = "display_mosi"
CONF_DISPLAY_CS = "display_cs"

CONF_STATUS_TEXT = "status_text"
CONF_STATUS_MESSAGE = "status_message"
CONF_ERROR_CODE = "error_code"
CONF_IS_MOWING = "is_mowing"
CONF_IS_CHARGING = "is_charging"
CONF_IS_DOCKED = "is_docked"
CONF_HAS_ERROR = "has_error"


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
            cv.Optional(CONF_STATUS_TEXT): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_STATUS_MESSAGE): text_sensor.text_sensor_schema(),
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

    if CONF_BATTERY_LEVEL in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_LEVEL])
        cg.add(var.set_battery_level_sensor(sens))

    if CONF_BATTERY_VOLTAGE in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_VOLTAGE])
        cg.add(var.set_battery_voltage_sensor(sens))

    if CONF_ERROR_CODE in config:
        sens = await sensor.new_sensor(config[CONF_ERROR_CODE])
        cg.add(var.set_error_code_sensor(sens))

    if CONF_STATUS_TEXT in config:
        sens = await text_sensor.new_text_sensor(config[CONF_STATUS_TEXT])
        cg.add(var.set_status_text_sensor(sens))

    if CONF_STATUS_MESSAGE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_STATUS_MESSAGE])
        cg.add(var.set_status_message_sensor(sens))

    if CONF_IS_MOWING in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_IS_MOWING])
        cg.add(var.set_is_mowing_sensor(sens))

    if CONF_IS_CHARGING in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_IS_CHARGING])
        cg.add(var.set_is_charging_sensor(sens))

    if CONF_IS_DOCKED in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_IS_DOCKED])
        cg.add(var.set_is_docked_sensor(sens))

    if CONF_HAS_ERROR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_HAS_ERROR])
        cg.add(var.set_has_error_sensor(sens))
