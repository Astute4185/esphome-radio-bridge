"""ESPHome radio TCP bridge with pluggable board and firmware drivers.

Board drivers own GPIO mapping, UART flow control, radio identification and
bootloader handling. Firmware catalogue support is optional and radio agnostic.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import (
    binary_sensor,
    button,
    esp32,
    http_request,
    select,
    sensor,
    switch,
    text,
    text_sensor,
)
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from esphome.core import CORE

DOMAIN = "esphome-radio-bridge"
DEPENDENCIES = ["esp32", "network"]
AUTO_LOAD = [
    "binary_sensor",
    "button",
    "http_request",
    "json",
    "select",
    "sensor",
    "switch",
    "text",
    "text_sensor",
]

# Add a new board ID here and its corresponding C++ BoardModel/factory implementation.
BOARD_DRIVERS = ("tubeszb_mgm210_poe_2023",)

CONF_DRIVER = "driver"
CONF_TCP_PORT = "tcp_port"
CONF_MAINTENANCE_TIMEOUT = "maintenance_timeout"
CONF_CONNECTED = "connected"
CONF_BRIDGE_READY = "bridge_ready"
CONF_BRIDGE_STATUS = "bridge_status"
CONF_LAST_DISCONNECT_REASON = "last_disconnect_reason"
CONF_TRANSPORT_ERRORS = "transport_errors"
CONF_DISCONNECTS = "disconnects"
CONF_MODE = "mode"
CONF_UART_MODE_SELECT = "uart_mode_select"
CONF_UART_MODE_STATUS = "uart_mode_status"
CONF_FLOW_PINS_VERIFIED = "flow_control_wiring_verified"
UART_EXPERIMENT_OPTIONS = [
    "2-wire (TX/RX)",
    "4-wire (profile RTS/CTS)",
    "4-wire (swapped RTS/CTS)",
]
CONF_FLASH_ARMED = "flash_armed"
CONF_RADIO_RESET = "radio_reset"
CONF_ENTER_BOOTLOADER = "enter_bootloader"
CONF_EXIT_BOOTLOADER = "exit_bootloader"
CONF_INSTALLED_FIRMWARE = "installed_firmware"
CONF_RADIO_IDENTIFICATION_STATUS = "radio_identification_status"
CONF_PROBE_RADIO = "probe_radio"
CONF_CATALOGUE_URL = "catalogue_url"
CONF_HTTP_REQUEST_ID = "http_request_id"
CONF_MANIFEST_OVERRIDE = "manifest_override"
CONF_CATALOGUE_STATUS = "catalogue_status"
CONF_REFRESH_CATALOGUE = "refresh_catalogue"
CONF_CHECK_UPDATES = "check_updates"
CONF_LATEST_FIRMWARE = "latest_firmware"
CONF_UPDATE_AVAILABLE = "update_available"
CONF_FLASH_FIRMWARE = "flash_firmware"
CONF_FIRMWARE_UPDATE_STATUS = "firmware_update_status"

# Board-specific staging requirements are registered only when an on-device
# firmware flash button is configured. Other drivers can provide their own
# requirements without changing the generic bridge or its UART implementation.
FLASH_STAGING = {
    "tubeszb_mgm210_poe_2023": ("radio_fw", "data", "undefined", 0x80000),
}
MIN_FIRMWARE_HTTP_TX_BUFFER = 4096


def secure_url(value):
    value = cv.url(value)
    if not value.startswith("https://"):
        raise cv.Invalid("Firmware catalogue must use HTTPS")
    return value


ns = cg.esphome_ns.namespace("esphome_radio_bridge")
RadioBridge = ns.class_("RadioBridge", cg.Component)
BoardModel = ns.namespace("drivers").enum("BoardModel", is_class=True)
RadioResetButton = ns.class_("RadioResetButton", button.Button)
ProbeRadioButton = ns.class_("ProbeRadioButton", button.Button)
RefreshCatalogueButton = ns.class_("RefreshCatalogueButton", button.Button)
CheckUpdatesButton = ns.class_("CheckUpdatesButton", button.Button)
FlashFirmwareButton = ns.class_("FlashFirmwareButton", button.Button)
ManifestOverrideText = ns.class_("ManifestOverrideText", text.Text)
EnterBootloaderButton = ns.class_("EnterBootloaderButton", button.Button)
ExitBootloaderButton = ns.class_("ExitBootloaderButton", button.Button)
FlashArmedSwitch = ns.class_("FlashArmedSwitch", switch.Switch)
UartModeSelect = ns.class_("UartModeSelect", select.Select)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(RadioBridge),
            cv.Required(CONF_DRIVER): cv.one_of(*BOARD_DRIVERS, lower=True),
            cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(http_request.HttpRequestComponent),
            cv.Optional(CONF_CATALOGUE_URL): secure_url,
            cv.Optional(CONF_FLOW_PINS_VERIFIED, default=False): cv.boolean,
            cv.Optional(CONF_UART_MODE_SELECT): select.select_schema(
                UartModeSelect, entity_category=ENTITY_CATEGORY_CONFIG
            ),
            cv.Optional(CONF_UART_MODE_STATUS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_MANIFEST_OVERRIDE): text.text_schema(
                ManifestOverrideText, mode="text", entity_category=ENTITY_CATEGORY_CONFIG
            ),
            cv.Optional(CONF_CATALOGUE_STATUS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_REFRESH_CATALOGUE): button.button_schema(
                RefreshCatalogueButton, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_CHECK_UPDATES): button.button_schema(
                CheckUpdatesButton, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_LATEST_FIRMWARE): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_FLASH_FIRMWARE): button.button_schema(
                FlashFirmwareButton, entity_category=ENTITY_CATEGORY_CONFIG
            ),
            cv.Optional(CONF_FIRMWARE_UPDATE_STATUS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_UPDATE_AVAILABLE): binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_TCP_PORT, default=6638): cv.port,
            cv.Optional(CONF_MAINTENANCE_TIMEOUT, default="10min"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_BRIDGE_READY): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_BRIDGE_STATUS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_LAST_DISCONNECT_REASON): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_TRANSPORT_ERRORS): sensor.sensor_schema(
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
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
            cv.Optional(CONF_INSTALLED_FIRMWARE): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_RADIO_IDENTIFICATION_STATUS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_PROBE_RADIO): button.button_schema(
                ProbeRadioButton, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
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


def _register_flash_partition(config):
    if CONF_FLASH_FIRMWARE not in config:
        return
    partition_spec = FLASH_STAGING.get(config[CONF_DRIVER])
    if partition_spec is None:
        return
    esp32_partitions = CORE.config.get("esp32", {}).get("partitions")
    if isinstance(esp32_partitions, list):
        # Keep existing user configurations compatible when migrating to the
        # driver-managed partition. ESPHome validates custom partition entries.
        if any(item.get("name") == partition_spec[0] for item in esp32_partitions):
            return
    elif esp32_partitions is not None:
        raise cv.Invalid(
            "A custom ESP32 partition CSV is configured. Include a 512 KiB "
            "radio_fw data partition in that CSV for firmware flashing."
        )
    esp32.add_partition(*partition_spec)


def _validate_firmware_http_request(config):
    if CONF_FLASH_FIRMWARE not in config:
        return
    request_config = fv.full_config.get().get("http_request") or {}
    if request_config.get("buffer_size_tx", 512) < MIN_FIRMWARE_HTTP_TX_BUFFER:
        raise cv.Invalid(
            "On-device firmware flashing requires "
            "http_request.buffer_size_tx: 4096 or greater to handle "
            "GitHub Release redirects",
            path=[CONF_FLASH_FIRMWARE],
        )
    if request_config.get("follow_redirects", True) is False:
        raise cv.Invalid(
            "On-device firmware flashing requires http_request.follow_redirects: true",
            path=[CONF_FLASH_FIRMWARE],
        )
    if request_config.get("verify_ssl", True) is False:
        raise cv.Invalid(
            "On-device firmware flashing requires http_request.verify_ssl: true",
            path=[CONF_FLASH_FIRMWARE],
        )


FINAL_VALIDATE_SCHEMA = _validate_firmware_http_request


async def to_code(config):
    _register_flash_partition(config)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_board_model(getattr(BoardModel, config[CONF_DRIVER].upper())))
    request = await cg.get_variable(config[CONF_HTTP_REQUEST_ID])
    cg.add(var.set_http_request(request))
    if catalogue_url := config.get(CONF_CATALOGUE_URL):
        cg.add(var.set_catalogue_url(catalogue_url))
    cg.add(var.set_flow_control_wiring_verified(config[CONF_FLOW_PINS_VERIFIED]))
    cg.add(var.set_tcp_port(config[CONF_TCP_PORT]))
    cg.add(var.set_maintenance_timeout(config[CONF_MAINTENANCE_TIMEOUT].total_milliseconds))

    if conf := config.get(CONF_UART_MODE_SELECT):
        entity = cg.new_Pvariable(conf[CONF_ID], var)
        await select.register_select(entity, conf, options=UART_EXPERIMENT_OPTIONS)
        cg.add(var.set_uart_mode_select(entity))
    if conf := config.get(CONF_UART_MODE_STATUS):
        entity = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_uart_mode_status_sensor(entity))
    if conf := config.get(CONF_MANIFEST_OVERRIDE):
        override = cg.new_Pvariable(conf[CONF_ID], var)
        await text.register_text(override, conf, min_length=0, max_length=255)
        cg.add(var.set_manifest_override_text(override))
    if conf := config.get(CONF_CATALOGUE_STATUS):
        status = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_catalogue_status_sensor(status))
    if conf := config.get(CONF_REFRESH_CATALOGUE):
        await button.new_button(conf, var)
    if conf := config.get(CONF_CHECK_UPDATES):
        await button.new_button(conf, var)
    if conf := config.get(CONF_LATEST_FIRMWARE):
        entity = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_latest_firmware_sensor(entity))
    if conf := config.get(CONF_FLASH_FIRMWARE):
        await button.new_button(conf, var)
    if conf := config.get(CONF_FIRMWARE_UPDATE_STATUS):
        entity = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_firmware_update_status_sensor(entity))
    if conf := config.get(CONF_UPDATE_AVAILABLE):
        entity = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_update_available_sensor(entity))
    if conf := config.get(CONF_BRIDGE_READY):
        entity = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_bridge_ready_sensor(entity))
    if conf := config.get(CONF_BRIDGE_STATUS):
        entity = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_bridge_status_sensor(entity))
    if conf := config.get(CONF_LAST_DISCONNECT_REASON):
        entity = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_last_disconnect_reason_sensor(entity))
    if conf := config.get(CONF_TRANSPORT_ERRORS):
        entity = await sensor.new_sensor(conf)
        cg.add(var.set_transport_errors_sensor(entity))
    if conf := config.get(CONF_CONNECTED):
        entity = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_connected_sensor(entity))
    if conf := config.get(CONF_DISCONNECTS):
        entity = await sensor.new_sensor(conf)
        cg.add(var.set_disconnects_sensor(entity))
    if conf := config.get(CONF_MODE):
        entity = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_mode_sensor(entity))
    if conf := config.get(CONF_INSTALLED_FIRMWARE):
        entity = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_installed_firmware_sensor(entity))
    if conf := config.get(CONF_RADIO_IDENTIFICATION_STATUS):
        entity = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_radio_identification_status_sensor(entity))
    if conf := config.get(CONF_PROBE_RADIO):
        await button.new_button(conf, var)
    if conf := config.get(CONF_FLASH_ARMED):
        entity = await switch.new_switch(conf, var)
        cg.add(var.set_armed_switch(entity))
    for name in (CONF_RADIO_RESET, CONF_ENTER_BOOTLOADER, CONF_EXIT_BOOTLOADER):
        if conf := config.get(name):
            await button.new_button(conf, var)
