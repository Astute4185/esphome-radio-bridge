#pragma once

#include <cstdint>

namespace esphome::esphome_radio_bridge {

// Bridge service health is separate from a connected ZHA client or radio protocol health.
enum class BridgeHealth {
  STARTING,
  IDENTIFYING,
  NETWORK_OFFLINE,
  LISTENER_UNAVAILABLE,
  MAINTENANCE,
  READY,
  FAILED,
  STOPPED
};
enum class DisconnectReason { REMOTE_CLOSED, SOCKET_ERROR, NETWORK_LOST, MAINTENANCE, SHUTDOWN };

// uint32_t unsigned subtraction remains valid when millis() wraps.
constexpr bool listener_retry_due(bool attempted, uint32_t now, uint32_t last_attempt, uint32_t interval) {
  return !attempted || static_cast<uint32_t>(now - last_attempt) >= interval;
}

constexpr bool bridge_is_ready(BridgeHealth health) { return health == BridgeHealth::READY; }

constexpr BridgeHealth evaluate_bridge_health(bool uart_ready, bool failed, bool network_ready,
                                              bool listener_ready, bool maintenance,
                                              bool identifying = false) {
  if (failed) return BridgeHealth::FAILED;
  if (!uart_ready) return BridgeHealth::STARTING;
  if (identifying) return BridgeHealth::IDENTIFYING;
  if (maintenance) return BridgeHealth::MAINTENANCE;
  if (!network_ready) return BridgeHealth::NETWORK_OFFLINE;
  if (!listener_ready) return BridgeHealth::LISTENER_UNAVAILABLE;
  return BridgeHealth::READY;
}

constexpr const char* bridge_health_name(BridgeHealth health) {
  switch (health) {
    case BridgeHealth::STARTING:
      return "Starting";
    case BridgeHealth::IDENTIFYING:
      return "Identifying radio";
    case BridgeHealth::NETWORK_OFFLINE:
      return "Network offline";
    case BridgeHealth::LISTENER_UNAVAILABLE:
      return "TCP listener unavailable";
    case BridgeHealth::MAINTENANCE:
      return "Maintenance";
    case BridgeHealth::READY:
      return "Ready";
    case BridgeHealth::FAILED:
      return "Failed";
    case BridgeHealth::STOPPED:
      return "Stopped";
  }
  return "Unknown";
}

constexpr const char* disconnect_reason_name(DisconnectReason reason) {
  switch (reason) {
    case DisconnectReason::REMOTE_CLOSED:
      return "Remote closed";
    case DisconnectReason::SOCKET_ERROR:
      return "TCP socket error";
    case DisconnectReason::NETWORK_LOST:
      return "Network lost";
    case DisconnectReason::MAINTENANCE:
      return "Maintenance timeout";
    case DisconnectReason::SHUTDOWN:
      return "ESP32 shutdown";
  }
  return "Unknown";
}

// A clean remote close may be intentional (for example, a ZHA restart).
// Only definite network/socket failures are counted as transport errors.
constexpr bool is_transport_error(DisconnectReason reason) {
  return reason == DisconnectReason::SOCKET_ERROR || reason == DisconnectReason::NETWORK_LOST;
}

}  // namespace esphome::esphome_radio_bridge
