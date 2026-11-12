#include <cassert>
#include <cstdint>
#include <string>

#include "firmware_update_monitor.h"

using namespace esphome::esphome_radio_bridge::firmware;

static FirmwareRelease release(const std::string& fw_version, const std::string& radio_version) {
  return {fw_version, radio_version, "release", std::string(64, 'a'),
          "https://github.com/owner/repo/releases/download/v1/fw.gbl"};
}

int main() {
  constexpr uint32_t DAY = UPDATE_CHECK_INTERVAL_MS;
  constexpr uint32_t RETRY = UPDATE_RETRY_INTERVAL_MS;
  assert(DAY == 86400000UL);
  assert(RETRY == 900000UL);
  assert(update_check_due(false, 0, 0, false));
  assert(!update_check_due(true, DAY - 1, 0, true));
  assert(update_check_due(true, DAY, 0, true));
  assert(!update_check_due(true, RETRY - 1, 0, false));
  assert(update_check_due(true, RETRY, 0, false));
  // uint32_t millis() rollover must preserve retry and daily scheduling.
  const uint32_t near_wrap = UINT32_MAX - 200U;
  assert(!update_check_due(true, 100U, near_wrap, false));
  assert(update_check_due(true, near_wrap + DAY, near_wrap, true));

  assert(compare_radio_versions("9.1.2", "9.1.2.0") == 0);
  assert(compare_radio_versions("9.1.2.0 (build 250)", "9.1.2") == 0);
  assert(compare_radio_versions("9.0.1", "9.1.2") < 0);
  assert(compare_radio_versions("9.2.0", "9.1.2") > 0);
  assert(compare_radio_versions("Unknown", "9.1.2") == 2);
  assert(compare_radio_versions("9.1.2 (build bad)", "9.1.2") == 2);
  assert(compare_radio_versions("9.1..2", "9.1.2") == 2);
  assert(compare_radio_versions("9.1.2.", "9.1.2") == 2);
  assert(compare_radio_versions("9.1.2-beta", "9.1.2") == 2);
  assert(compare_radio_versions("9.1.65536", "9.1.2") == 2);

  FirmwareLoadResult data;
  data.latest = "1.0";
  data.versions.push_back(release("1.0", "9.1.2"));
  assert(assess_update(data, "9.0.0", true).availability == UpdateAvailability::UNKNOWN);
  data.ok = true;
  assert(assess_update(data, "9.0.0", false).availability == UpdateAvailability::UNKNOWN);
  assert(assess_update(data, "Unknown", true).availability == UpdateAvailability::UNKNOWN);
  assert(assess_update(data, "9.0.0", true).availability == UpdateAvailability::AVAILABLE);
  assert(assess_update(data, "9.1.2.0 (build 410)", true).availability == UpdateAvailability::CURRENT);
  assert(assess_update(data, "9.2.0", true).availability == UpdateAvailability::CURRENT);

  data.versions.push_back(release("0.9", "9.0.0"));
  assert(assess_update(data, "9.0.0", true).availability == UpdateAvailability::AVAILABLE);
  assert(assess_update(data, "9.1.2", true).availability == UpdateAvailability::CURRENT);
  data.versions.push_back(release("0.8", "9.1.2.0"));
  assert(assess_update(data, "9.1.2", true).availability == UpdateAvailability::UNKNOWN);
  data.versions.pop_back();
  data.latest = "missing";
  assert(assess_update(data, "9.0.0", true).availability == UpdateAvailability::UNKNOWN);
  data.latest.clear();
  data.versions.clear();
  assert(assess_update(data, "Unknown", false).availability == UpdateAvailability::CURRENT);
}
