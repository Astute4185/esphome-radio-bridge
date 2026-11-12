#include <cassert>
#include <string>

#include "bridge_health.h"

using namespace esphome::esphome_radio_bridge;

int main() {
  assert(evaluate_bridge_health(false, false, false, false, false) == BridgeHealth::STARTING);
  assert(evaluate_bridge_health(true, true, true, true, false) == BridgeHealth::FAILED);
  assert(evaluate_bridge_health(true, false, false, false, false) == BridgeHealth::NETWORK_OFFLINE);
  assert(evaluate_bridge_health(true, false, true, false, false) == BridgeHealth::LISTENER_UNAVAILABLE);
  assert(evaluate_bridge_health(true, false, true, true, true) == BridgeHealth::MAINTENANCE);
  assert(evaluate_bridge_health(true, false, true, true, false) == BridgeHealth::READY);
  assert(bridge_is_ready(BridgeHealth::READY));
  assert(!bridge_is_ready(BridgeHealth::MAINTENANCE));
  assert(!bridge_is_ready(BridgeHealth::NETWORK_OFFLINE));
  assert(std::string(bridge_health_name(BridgeHealth::LISTENER_UNAVAILABLE)) == "TCP listener unavailable");
  assert(std::string(disconnect_reason_name(DisconnectReason::REMOTE_CLOSED)) == "Remote closed");
  assert(std::string(disconnect_reason_name(DisconnectReason::NETWORK_LOST)) == "Network lost");
  // First attempt is immediate, failures back off without blocking the main loop.
  assert(listener_retry_due(false, 1, 0, 5000));
  assert(!listener_retry_due(true, 4999, 0, 5000));
  assert(listener_retry_due(true, 5000, 0, 5000));
  assert(!listener_retry_due(true, 0x00000003, 0xFFFFFFF0, 100));
  assert(listener_retry_due(true, 0x00000080, 0xFFFFFFF0, 100));
  assert(is_transport_error(DisconnectReason::NETWORK_LOST));
  assert(is_transport_error(DisconnectReason::SOCKET_ERROR));
  assert(!is_transport_error(DisconnectReason::REMOTE_CLOSED));
  assert(!is_transport_error(DisconnectReason::MAINTENANCE));
  assert(!is_transport_error(DisconnectReason::SHUTDOWN));
}
