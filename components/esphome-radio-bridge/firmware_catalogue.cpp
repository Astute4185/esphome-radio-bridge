#include "firmware_catalogue.h"

#include <atomic>
#include <utility>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/json/json_util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::esphome_radio_bridge::firmware {

static const char* const TAG = "radio_bridge.catalogue";
static constexpr size_t MAX_JSON_BYTES = 16384;
static constexpr size_t MAX_RELEASES = 48;
static constexpr uint32_t MAX_FETCH_MS = 20000;

struct FirmwareCatalogue::Job {
  http_request::HttpRequestComponent* request{nullptr};
  std::string catalogue_url;
  std::string manifest_override;
  std::string board;
  std::string radio_type;
  FirmwareLoadResult result;
  std::atomic<bool> finished{false};
};

namespace {

bool download_json(http_request::HttpRequestComponent* request, const std::string& url, std::string& body,
                   std::string& error) {
  if (!is_https_url(url)) {
    error = "Non-HTTPS metadata URL rejected";
    return false;
  }
  auto response = request->get(url);
  if (response == nullptr) {
    error = "HTTPS request failed";
    return false;
  }
  if (response->status_code != http_request::HTTP_STATUS_OK) {
    error = "HTTP status " + std::to_string(response->status_code);
    response->end();
    return false;
  }
  if (response->content_length > MAX_JSON_BYTES) {
    error = "Metadata exceeds 16 KiB";
    response->end();
    return false;
  }
  char buffer[512];
  const uint32_t started = millis();
  while (!response->is_read_complete()) {
    if (static_cast<uint32_t>(millis() - started) > MAX_FETCH_MS) {
      error = "Metadata download timed out";
      response->end();
      return false;
    }
    const int got = response->read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
    if (got < 0) {
      error = "Metadata download failed";
      response->end();
      return false;
    }
    if (got == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (body.size() + static_cast<size_t>(got) > MAX_JSON_BYTES) {
      error = "Metadata exceeds 16 KiB";
      response->end();
      return false;
    }
    body.append(buffer, static_cast<size_t>(got));
  }
  response->end();
  if (body.empty()) {
    error = "Empty JSON response";
    return false;
  }
  return true;
}

bool parse_catalogue(const std::string& body, const std::string& catalogue_url, const std::string& board,
                     const std::string& radio_type, std::string& manifest_url, std::string& error) {
  bool valid = json::parse_json(body, [&](JsonObject root) -> bool {
    if (root["schema_version"].as<int>() != 1 || !root["boards"].is<JsonObject>()) return false;
    const JsonObject boards = root["boards"].as<JsonObject>();
    if (!boards[board].is<JsonObject>()) return false;
    const JsonObject radios = boards[board].as<JsonObject>();
    if (!radios[radio_type].is<const char*>()) return false;
    return resolve_manifest_url(catalogue_url, radios[radio_type].as<std::string>(), manifest_url);
  });
  if (!valid) error = "Catalogue invalid, unsafe path, or unsupported board/radio";
  return valid;
}

bool parse_manifest(const std::string& body, const std::string& board, const std::string& radio_type,
                    FirmwareLoadResult& out, std::string& error) {
  const bool parsed = json::parse_json(body, [&](JsonObject root) -> bool {
    if (root["schema_version"].as<int>() != 1) {
      error = "Unsupported manifest schema_version (expected 1)";
      return false;
    }
    if (!root["board"].is<const char*>()) {
      error = "Manifest board is missing or not a string";
      return false;
    }
    if (root["board"].as<std::string>() != board) {
      error = "Manifest board mismatch (expected " + board + ")";
      return false;
    }
    if (!root["radio_type"].is<const char*>()) {
      error = "Manifest radio_type is missing or not a string";
      return false;
    }
    if (root["radio_type"].as<std::string>() != radio_type) {
      error = "Manifest radio_type mismatch (expected " + radio_type + ")";
      return false;
    }
    if (!root["versions"].is<JsonArray>()) {
      error = "Manifest versions must be an array";
      return false;
    }
    const JsonArray entries = root["versions"].as<JsonArray>();
    if (entries.size() > MAX_RELEASES) {
      error = "Manifest exceeds maximum release count";
      return false;
    }
    if (root["latest"].isNull()) {
      if (entries.size() != 0) {
        error = "Manifest latest cannot be null when versions are present";
        return false;
      }
      out.latest.clear();
    } else if (root["latest"].is<const char*>()) {
      out.latest = root["latest"].as<std::string>();
      if (!valid_firmware_version(out.latest)) {
        error = "Invalid manifest latest version";
        return false;
      }
    } else {
      error = "Manifest latest must be a version string or null";
      return false;
    }

    std::vector<FirmwareRelease> releases;
    releases.reserve(entries.size());
    size_t release_index = 0;
    for (JsonVariant entry_variant : entries) {
      if (!entry_variant.is<JsonObject>()) {
        error = "Manifest versions[" + std::to_string(release_index) + "] must be an object";
        return false;
      }
      const JsonObject entry = entry_variant.as<JsonObject>();
      for (const char* field : {"firmware_version", "radio_version", "release_type", "sha256", "file"}) {
        if (!entry[field].is<const char*>()) {
          error = "Manifest versions[" + std::to_string(release_index) + "]." + field +
                  " is missing or not a string";
          return false;
        }
      }
      FirmwareRelease release{entry["firmware_version"].as<std::string>(),
                              entry["radio_version"].as<std::string>(),
                              entry["release_type"].as<std::string>(), entry["sha256"].as<std::string>(),
                              entry["file"].as<std::string>()};
      if (const char* reason = release_validation_error(release); reason != nullptr) {
        error = "Manifest versions[" + std::to_string(release_index) + "]: " + reason;
        return false;
      }
      releases.push_back(std::move(release));
      ++release_index;
    }
    out.board = board;
    out.radio_type = radio_type;
    out.versions = std::move(releases);
    if (!valid_manifest_metadata(out)) {
      const char* reason = manifest_validation_error(out);
      error = reason == nullptr ? "Manifest release metadata rejected" : reason;
      return false;
    }
    return true;
  });
  if (!parsed && error.empty()) error = "Manifest is not valid JSON or could not be parsed";
  return parsed;
}

}  // namespace

bool FirmwareCatalogue::start(http_request::HttpRequestComponent* request, const std::string& catalogue_url,
                              const std::string& manifest_override, const std::string& board,
                              const std::string& radio_type) {
  if (job_ != nullptr || request == nullptr || board.empty() || radio_type.empty() ||
      (manifest_override.empty() && !is_https_url(catalogue_url)) ||
      (!manifest_override.empty() && !is_https_url(manifest_override)))
    return false;
  auto* job = new Job();
  job->request = request;
  job->catalogue_url = catalogue_url;
  job->manifest_override = manifest_override;
  job->board = board;
  job->radio_type = radio_type;
  job_ = job;
  if (xTaskCreate(&FirmwareCatalogue::worker_, "radio_catalogue", 12288, job, 1, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Unable to create firmware catalogue task");
    job_ = nullptr;
    delete job;
    return false;
  }
  return true;
}

void FirmwareCatalogue::worker_(void* param) {
  auto* job = static_cast<Job*>(param);
  auto& out = job->result;
  std::string error;
  std::string manifest_url = job->manifest_override;
  do {
    if (manifest_url.empty()) {
      std::string catalogue_body;
      if (!download_json(job->request, job->catalogue_url, catalogue_body, error)) break;
      if (!parse_catalogue(catalogue_body, job->catalogue_url, job->board, job->radio_type, manifest_url,
                           error))
        break;
    }
    out.manifest_url = manifest_url;
    std::string manifest_body;
    if (!download_json(job->request, manifest_url, manifest_body, error)) break;
    if (!parse_manifest(manifest_body, job->board, job->radio_type, out, error)) {
      // Avoid logging query strings, which may contain credentials in custom URLs.
      ESP_LOGW(TAG, "Manifest rejected for board=%s radio=%s source=%s: %s", job->board.c_str(),
               job->radio_type.c_str(), manifest_url.substr(0, manifest_url.find('?')).c_str(),
               error.c_str());
      break;
    }
    out.ok = true;
    out.message = out.latest.empty() ? "Loaded: no releases" : "Loaded: " + out.latest;
  } while (false);
  if (!out.ok) out.message = error.empty() ? "Catalogue failed" : error;
  job->finished.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

bool FirmwareCatalogue::collect(FirmwareLoadResult& out) {
  if (job_ == nullptr || !job_->finished.load(std::memory_order_acquire)) return false;
  out = std::move(job_->result);
  delete job_;
  job_ = nullptr;
  return true;
}

}  // namespace esphome::esphome_radio_bridge::firmware
