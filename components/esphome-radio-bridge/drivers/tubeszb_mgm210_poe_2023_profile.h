#pragma once
#include "board_driver.h"
namespace esphome::serial_radio_gateway::drivers {
// MGM210 PoE 2023: 4-wire RTS/CTS NCP application, 2-wire Gecko bootloader.
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
};
static_assert(valid_profile(TUBESZB_MGM210_POE_2023), "Invalid TubesZB board mapping");
}  // namespace esphome::serial_radio_gateway::drivers
