import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_BATTERY_LEVEL,
)
from esphome.components import uart, sensor, binary_sensor, text_sensor
from esphome.core import CORE

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor", "json"]

snk_mower_ns = cg.esphome_ns.namespace("snk_mower")
SnkMower = snk_mower_ns.class_("SnkMower", cg.Component, uart.UARTDevice)

CONF_PIN = "pin"
CONF_DISPLAY_CLK = "display_clk"
CONF_DISPLAY_MOSI = "display_mosi"
CONF_DISPLAY_CS = "display_cs"
CONF_BUZZER_PIN = "buzzer_pin"
CONF_DISPLAY_OFF_TIMEOUT = "display_off_timeout"
CONF_DISPLAY_NIGHT_BRIGHTNESS = "display_night_brightness"
CONF_RAIN_PIN = "rain_pin"
CONF_RAIN_DRIVE_A = "rain_drive_a"
CONF_RAIN_DRIVE_B = "rain_drive_b"
CONF_RAIN_THRESHOLD = "rain_threshold"
CONF_RAIN_ADC = "rain_adc"
CONF_RAINING = "raining"

CONF_ERROR_CODE = "error_code"
CONF_IS_MOWING = "is_mowing"
CONF_IS_CHARGING = "is_charging"
CONF_IS_DOCKED = "is_docked"
CONF_IS_RETURNING = "is_returning"

CONF_LIGHT_LEVEL = "light_level"
CONF_WORK_AREA = "work_area"
CONF_CUT_AREA = "cut_area"
CONF_TOTAL_MINUTES = "total_minutes"
CONF_ON_MINUTES = "on_minutes"
CONF_BAT_HEALTH = "bat_health"
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
            cv.Required(CONF_PIN): validate_pin,
            cv.Optional(CONF_DISPLAY_CLK, default=33): cv.int_range(0, 39),
            cv.Optional(CONF_DISPLAY_MOSI, default=25): cv.int_range(0, 39),
            cv.Optional(CONF_DISPLAY_CS, default=32): cv.int_range(0, 39),
            cv.Optional(CONF_BUZZER_PIN): cv.int_range(0, 39),
            cv.Optional(CONF_DISPLAY_OFF_TIMEOUT, default=0): cv.positive_int,
            # Display brightness in night mode (set_display_night), percent of full.
            cv.Optional(CONF_DISPLAY_NIGHT_BRIGHTNESS, default=20): cv.int_range(1, 100),
            # Rain sensor: ADC1 input (GPIO36 on the display board) and the
            # two electrode drive pins, see docs/hardware.md.
            cv.Optional(CONF_RAIN_PIN): cv.int_range(32, 39),
            cv.Optional(CONF_RAIN_DRIVE_A, default=18): cv.int_range(0, 33),
            cv.Optional(CONF_RAIN_DRIVE_B, default=5): cv.int_range(0, 33),
            cv.Optional(CONF_RAIN_THRESHOLD, default=3000): cv.int_range(1, 4095),
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement="%",
                accuracy_decimals=0,
                device_class="battery",
            ),
            cv.Optional(CONF_ERROR_CODE): sensor.sensor_schema(
                icon="mdi:alert-circle",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_LIGHT_LEVEL): sensor.sensor_schema(
                icon="mdi:brightness-5",
                accuracy_decimals=0,
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_WORK_AREA): sensor.sensor_schema(
                unit_of_measurement="m²",
                icon="mdi:map-marker-area",
                accuracy_decimals=0,
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_CUT_AREA): sensor.sensor_schema(
                unit_of_measurement="m²",
                icon="mdi:grass",
                accuracy_decimals=0,
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_TOTAL_MINUTES): sensor.sensor_schema(
                unit_of_measurement="min",
                device_class="duration",
                icon="mdi:clock-outline",
                accuracy_decimals=0,
                state_class="total_increasing",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_ON_MINUTES): sensor.sensor_schema(
                unit_of_measurement="min",
                device_class="duration",
                icon="mdi:timer-outline",
                accuracy_decimals=0,
                state_class="total_increasing",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_BAT_HEALTH): sensor.sensor_schema(
                unit_of_measurement="%",
                icon="mdi:heart-pulse",
                accuracy_decimals=0,
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_RAIN_DELAY): sensor.sensor_schema(
                unit_of_measurement="min",
                device_class="duration",
                icon="mdi:weather-rainy",
                accuracy_decimals=0,
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_RAIN_ADC): sensor.sensor_schema(
                icon="mdi:water",
                accuracy_decimals=0,
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_RAINING): binary_sensor.binary_sensor_schema(
                device_class="moisture",
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
            cv.Optional(CONF_IS_RETURNING): binary_sensor.binary_sensor_schema(
                icon="mdi:home-import-outline",
            ),
            cv.Optional(CONF_DEVICE_NAME): text_sensor.text_sensor_schema(
                icon="mdi:label",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_MODEL): text_sensor.text_sensor_schema(
                icon="mdi:information",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_SERIAL): text_sensor.text_sensor_schema(
                icon="mdi:barcode",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_FIRMWARE_VERSION): text_sensor.text_sensor_schema(
                icon="mdi:package-up",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_BATTERY_NAME): text_sensor.text_sensor_schema(
                icon="mdi:battery-info",
                entity_category="diagnostic",
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

    if CORE.is_esp32:
        # ESPHome excludes the ESP-IDF ADC driver from builds by default; the
        # rain sensor uses adc_oneshot, so pull esp_adc back in.
        from esphome.components.esp32 import include_builtin_idf_component
        include_builtin_idf_component("esp_adc")
        # The display is multiplexed from a GPTimer alarm.
        include_builtin_idf_component("esp_driver_gptimer")

    cg.add(var.set_display_pins(
        config[CONF_DISPLAY_CLK],
        config[CONF_DISPLAY_MOSI],
        config[CONF_DISPLAY_CS],
    ))

    if CONF_BUZZER_PIN in config:
        cg.add(var.set_buzzer_pin(cg.RawExpression(
            f'(gpio_num_t){config[CONF_BUZZER_PIN]}')))

    if CONF_RAIN_PIN in config:
        cg.add(var.set_rain_pin(cg.RawExpression(
            f'(gpio_num_t){config[CONF_RAIN_PIN]}')))
        cg.add(var.set_rain_drive_pins(
            cg.RawExpression(f'(gpio_num_t){config[CONF_RAIN_DRIVE_A]}'),
            cg.RawExpression(f'(gpio_num_t){config[CONF_RAIN_DRIVE_B]}')))
        cg.add(var.set_rain_threshold(config[CONF_RAIN_THRESHOLD]))

    cg.add(var.set_display_night_brightness(config[CONF_DISPLAY_NIGHT_BRIGHTNESS]))

    if config[CONF_DISPLAY_OFF_TIMEOUT] > 0:
        cg.add(var.set_display_off_timeout(config[CONF_DISPLAY_OFF_TIMEOUT]))

    for key, setter in [
        (CONF_BATTERY_LEVEL, "set_battery_level_sensor"),
        (CONF_ERROR_CODE, "set_error_code_sensor"),
        (CONF_LIGHT_LEVEL, "set_light_level_sensor"),
        (CONF_WORK_AREA, "set_work_area_sensor"),
        (CONF_CUT_AREA, "set_cut_area_sensor"),
        (CONF_TOTAL_MINUTES, "set_total_minutes_sensor"),
        (CONF_ON_MINUTES, "set_on_minutes_sensor"),
        (CONF_BAT_HEALTH, "set_bat_health_sensor"),
        (CONF_RAIN_DELAY, "set_rain_delay_sensor"),
        (CONF_RAIN_ADC, "set_rain_adc_sensor"),
    ]:
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter in [
        (CONF_IS_MOWING, "set_is_mowing_sensor"),
        (CONF_IS_CHARGING, "set_is_charging_sensor"),
        (CONF_IS_DOCKED, "set_is_docked_sensor"),
        (CONF_IS_RETURNING, "set_is_returning_sensor"),
        (CONF_RAINING, "set_raining_sensor"),
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
