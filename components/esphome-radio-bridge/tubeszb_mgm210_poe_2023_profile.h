#pragma once
#include "board_driver.h"
namespace esphome::esphome_radio_bridge::drivers {
// MGM210PA32JIA (PoE 2023): verified 4-wire NCP and 2-wire Gecko bootloader.
// ESP32 GPIO2 RTS -> EFR32 PD02 CTS (MGM210 pad 17 -> ESP32 pad 24).
// EFR32 PC01 RTS -> ESP32 GPIO13 CTS (MGM210 pad 22 -> ESP32 pad 16).
// Normal UART is hardware-flow-controlled; bootloader UART disables flow control.
inline constexpr BoardProfile TUBESZB_MGM210_POE_2023{
    "tubeszb_mgm210_poe_2023",
    {1, 4, 36, 2, 13, 122},             // UART1, TX, RX, RTS, CTS, threshold
    uart_modes::UartModeId::FOUR_WIRE,  // normal mode
    uart_modes::UartModeId::TWO_WIRE,   // bootloader mode
    5,
    16,  // RESET, PA0 boot selection (both active-low)
    true,
    true,
    115200,
    115200,
    true,
    FirmwareProtocol::GECKO_XMODEM_CRC,
    "zha",
};
static_assert(valid_profile(TUBESZB_MGM210_POE_2023), "Invalid TubesZB board mapping");
}  // namespace esphome::esphome_radio_bridge::drivers
