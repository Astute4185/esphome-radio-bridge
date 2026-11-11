#pragma once

#include "board_driver.h"
#include "tubeszb_mgm210_poe_2023.h"

#include <memory>

namespace esphome::serial_radio_gateway::drivers {
enum class BoardModel : uint8_t { TUBESZB_MGM210_POE_2023 };

inline std::unique_ptr<BoardDriver> create_board_driver(BoardModel model) {
  switch (model) {
    case BoardModel::TUBESZB_MGM210_POE_2023:
      return std::make_unique<TubesZbMgm210Poe2023Driver>();
  }
  return nullptr;
}
}  // namespace esphome::serial_radio_gateway::drivers
