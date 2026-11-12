#pragma once
#include "board_driver.h"
namespace esphome::esphome_radio_bridge::drivers {
// MGM210 PoE 2023 POC: 2-wire NCP application and 2-wire Gecko bootloader.
// RTS/CTS physical pins remain mapped for a future 4-wire firmware migration.
inline constexpr BoardProfile TUBESZB_MGM210_POE_2023{
    "tubeszb_mgm210_poe_2023",
    {1, 4, 36, 2, 13, 122},             // UART1, TX, RX, RTS, CTS, threshold
    uart_modes::UartModeId::TWO_WIRE,   // normal mode (POC: no RTS/CTS)
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
