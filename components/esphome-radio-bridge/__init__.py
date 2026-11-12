"""ESP32/ESP-IDF serial radio TCP gateway with a pluggable board driver.

Every supported board must have normal-mode RTS/CTS wired. The board driver
owns GPIO mapping, reset, bootloader entry, baud rates and bootloader UART mode.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, button, sensor, switch, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

DOMAIN = "serial_radio_gateway"
DEPENDENCIES = ["esp32", "network"]
AUTO_LOAD = ["binary_sensor", "button", "sensor", "switch", "text_sensor"]

# Add a new board ID here and its corresponding C++ BoardModel/factory implementation.
BOARD_DRIVERS = ("tubeszb_mgm210_poe_2023",)

CONF_DRIVER = "driver"
CONF_TCP_PORT = "tcp_port"
CONF_MAINTENANCE_TIMEOUT = "maintenance_timeout"
CONF_CONNECTED = "connected"
CONF_DISCONNECTS = "disconnects"
CONF_MODE = "mode"
CONF_FLASH_ARMED = "flash_armed"
CONF_RADIO_RESET = "radio_reset"
CONF_ENTER_BOOTLOADER = "enter_bootloader"
CONF_EXIT_BOOTLOADER = "exit_bootloader"

ns = cg.esphome_ns.namespace(DOMAIN)
SerialRadioGateway = ns.class_("SerialRadioGateway", cg.Component)
BoardModel = ns.namespace("drivers").enum("BoardModel")
RadioResetButton = ns.class_("RadioResetButton", button.Button)
EnterBootloaderButton = ns.class_("EnterBootloaderButton", button.Button)
ExitBootloaderButton = ns.class_("ExitBootloaderButton", button.Button)
FlashArmedSwitch = ns.class_("FlashArmedSwitch", switch.Switch)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SerialRadioGateway),
            cv.Required(CONF_DRIVER): cv.one_of(*BOARD_DRIVERS, lower=True),
            cv.Optional(CONF_TCP_PORT, default=6638): cv.port,
            cv.Optional(CONF_MAINTENANCE_TIMEOUT, default="10min"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_DISCONNECTS): sensor.sensor_schema(
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_MODE): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_FLASH_ARMED): switch.switch_schema(
                FlashArmedSwitch,
                default_restore_mode="ALWAYS_OFF",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_RADIO_RESET): button.button_schema(
                RadioResetButton, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_ENTER_BOOTLOADER): button.button_schema(
                EnterBootloaderButton, entity_category=ENTITY_CATEGORY_CONFIG
            ),
            cv.Optional(CONF_EXIT_BOOTLOADER): button.button_schema(
                ExitBootloaderButton, entity_category=ENTITY_CATEGORY_CONFIG
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    cv.only_with_framework("esp-idf"),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_board_model(getattr(BoardModel, config[CONF_DRIVER].upper())))
    cg.add(var.set_tcp_port(config[CONF_TCP_PORT]))
    cg.add(var.set_maintenance_timeout(config[CONF_MAINTENANCE_TIMEOUT].total_milliseconds))

    if conf := config.get(CONF_CONNECTED):
        entity = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_connected_sensor(entity))
    if conf := config.get(CONF_DISCONNECTS):
        entity = await sensor.new_sensor(conf)
        cg.add(var.set_disconnects_sensor(entity))
    if conf := config.get(CONF_MODE):
        entity = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_mode_sensor(entity))
    if conf := config.get(CONF_FLASH_ARMED):
        entity = await switch.new_switch(conf, var)
        cg.add(var.set_armed_switch(entity))
    for name in (CONF_RADIO_RESET, CONF_ENTER_BOOTLOADER, CONF_EXIT_BOOTLOADER):
        if conf := config.get(name):
            await button.new_button(conf, var)
