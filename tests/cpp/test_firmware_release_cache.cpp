#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include "firmware_release_cache.h"

using namespace esphome::esphome_radio_bridge::firmware;

int main() {
  const auto standard = firmware_source_key("tubeszb_mgm210_poe_2023", "zha",
                                            "https://example.org/firmware/catalogue.json", "");
  const auto overridden =
      firmware_source_key("tubeszb_mgm210_poe_2023", "zha", "https://example.org/firmware/catalogue.json",
                          "https://custom.test/firmware/manifest.json");
  assert(standard != 0);
  assert(standard != overridden);
  assert(firmware_source_key("tubeszb_mgm210_poe_2023", "zha", "https://example.org/firmware/catalogue.json",
                             "") == standard);
  assert(standard !=
         firmware_source_key("another_board", "zha", "https://example.org/firmware/catalogue.json", ""));

  const std::vector<FirmwareChoice> releases = {
      {"Latest", ""},
      {"9.1.2 (release 0.1, beta)", "0.1"},
      {"9.1.2 (release 0.2, release)", "0.2"},
  };
  FirmwareChoicesSnapshot stored{};
  assert(make_firmware_choices_snapshot(releases, standard, "0.1", stored));
  assert(stored.count == 3);
  std::vector<FirmwareChoice> restored;
  std::string selection;
  assert(read_firmware_choices_snapshot(stored, standard, restored, selection));
  assert(same_firmware_choices(restored, releases));
  assert(selection == "0.1");
  assert(!read_firmware_choices_snapshot(stored, overridden, restored, selection));

  // The cache never contains release download URLs or SHA-256, and therefore
  // cannot be used as an authority for a firmware flash.
  assert(sizeof(stored.choices[0]) == RELEASE_CACHE_LABEL_BYTES + RELEASE_CACHE_RELEASE_ID_BYTES);

  auto damaged = stored;
  damaged.magic = 0;
  assert(!read_firmware_choices_snapshot(damaged, standard, restored, selection));
  damaged = stored;
  damaged.version = 123;
  assert(!read_firmware_choices_snapshot(damaged, standard, restored, selection));
  damaged = stored;
  damaged.count = RELEASE_CACHE_MAX_CHOICES + 1;
  assert(!read_firmware_choices_snapshot(damaged, standard, restored, selection));
  damaged = stored;
  std::memset(damaged.choices[1].label, 'x', sizeof(damaged.choices[1].label));
  assert(!read_firmware_choices_snapshot(damaged, standard, restored, selection));
  damaged = stored;
  std::strcpy(damaged.choices[2].release_id, "0.1");
  assert(!read_firmware_choices_snapshot(damaged, standard, restored, selection));
  damaged = stored;
  std::strcpy(damaged.selected_release_id, "0.3");
  assert(!read_firmware_choices_snapshot(damaged, standard, restored, selection));

  FirmwareChoicesSnapshot failed{};
  assert(!make_firmware_choices_snapshot(releases, standard, "0.3", failed));
  assert(!make_firmware_choices_snapshot(releases, 0, "0.1", failed));
  auto truncated = releases;
  truncated[1].label = std::string(RELEASE_CACHE_LABEL_BYTES, 'x');
  assert(!make_firmware_choices_snapshot(truncated, standard, "0.1", failed));
  std::vector<FirmwareChoice> too_many = releases;
  for (size_t i = 0; i < RELEASE_CACHE_MAX_CHOICES; ++i)
    too_many.push_back({"Other " + std::to_string(i), std::to_string(i + 3)});
  assert(!make_firmware_choices_snapshot(too_many, standard, "", failed));

  assert(same_firmware_choices(releases, releases));
  assert(!same_firmware_choices(releases, std::vector<FirmwareChoice>{{"Latest", ""}}));
  auto changed = releases;
  changed[2].label = "9.1.2 (release 0.2, beta)";
  assert(!same_firmware_choices(releases, changed));
}
