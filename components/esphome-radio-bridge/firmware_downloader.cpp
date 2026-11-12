#include "firmware_downloader.h"

#include <atomic>
#include <cstdint>
#include <utility>

#include "esphome/components/http_request/http_request.h"
#include "esphome/core/hal.h"
#include "flash_partition_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

namespace esphome::esphome_radio_bridge::firmware {

static constexpr size_t MAX_FIRMWARE_BYTES = 2U * 1024U * 1024U;
static constexpr uint32_t MAX_DOWNLOAD_MS = 180000;

struct FirmwareDownloader::Job {
  http_request::HttpRequestComponent* request{nullptr};
  FirmwareRelease release;
  DownloadedFirmware result;
  std::atomic<size_t> received{0};
  std::atomic<size_t> expected{0};
  std::atomic<bool> finished{false};
};

namespace {

bool digest_matches(const uint8_t* digest, const std::string& expected) {
  constexpr char HEX[] = "0123456789abcdef";
  for (size_t i = 0; i < 32; ++i) {
    char high = expected[2 * i];
    char low = expected[2 * i + 1];
    if (high >= 'A' && high <= 'F') high = static_cast<char>(high - 'A' + 'a');
    if (low >= 'A' && low <= 'F') low = static_cast<char>(low - 'A' + 'a');
    if (HEX[digest[i] >> 4] != high || HEX[digest[i] & 15] != low) return false;
  }
  return true;
}

}  // namespace

bool FirmwareDownloader::start(http_request::HttpRequestComponent* request, const FirmwareRelease& release) {
  if (job_ != nullptr || request == nullptr || !valid_release(release)) return false;
  auto* job = new Job();
  job->request = request;
  job->release = release;
  job_ = job;
  if (xTaskCreate(&FirmwareDownloader::worker_, "radio_flash_dl", 12288, job, 1, nullptr) != pdPASS) {
    job_ = nullptr;
    delete job;
    return false;
  }
  return true;
}

void FirmwareDownloader::worker_(void* parameter) {
  auto* job = static_cast<Job*>(parameter);
  auto& out = job->result;
  do {
    auto storage = FlashPartitionStorage::open();
    if (storage == nullptr) {
      out.message = "Missing radio_fw staging partition";
      break;
    }
    auto response = job->request->get(job->release.file);
    if (response == nullptr) {
      out.message = "HTTPS firmware request failed";
      break;
    }
    if (response->status_code != http_request::HTTP_STATUS_OK) {
      out.message = response->status_code >= 300 && response->status_code < 400
                        ? "GitHub Release redirect blocked; set http_request.follow_redirects: true"
                        : "Firmware HTTP status " + std::to_string(response->status_code);
      response->end();
      break;
    }
    const size_t length = response->content_length;
    // Require a bounded Content-Length; no live-streamed unverified bytes reach UART.
    if (length == 0 || length > MAX_FIRMWARE_BYTES || length > storage->capacity()) {
      out.message = "Firmware Content-Length missing or exceeds staging partition";
      response->end();
      break;
    }
    job->expected.store(length, std::memory_order_relaxed);
    if (!storage->prepare(length)) {
      out.message = "Unable to erase firmware staging partition";
      response->end();
      break;
    }
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool failed = mbedtls_sha256_starts(&hash, 0) != 0;
    if (failed) out.message = "SHA-256 initialisation failed";
    size_t received = 0;
    uint8_t chunk[1024];
    const uint32_t started = millis();
    while (!failed && !response->is_read_complete() && received < length) {
      if (static_cast<uint32_t>(millis() - started) >= MAX_DOWNLOAD_MS) {
        out.message = "Firmware download timed out";
        failed = true;
        break;
      }
      const size_t wanted = (length - received) < sizeof(chunk) ? length - received : sizeof(chunk);
      const int got = response->read(chunk, wanted);
      if (got < 0) {
        out.message = "Firmware download interrupted";
        failed = true;
        break;
      }
      if (got == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      if (!storage->write(received, chunk, static_cast<size_t>(got)) ||
          mbedtls_sha256_update(&hash, chunk, static_cast<size_t>(got)) != 0) {
        out.message = "Firmware staging write or checksum update failed";
        failed = true;
        break;
      }
      received += static_cast<size_t>(got);
      job->received.store(received, std::memory_order_relaxed);
    }
    response->end();
    if (!failed && received != length) {
      out.message = "Truncated firmware image";
      failed = true;
    }
    uint8_t digest[32]{};
    if (!failed && mbedtls_sha256_finish(&hash, digest) != 0) {
      out.message = "SHA-256 finalisation failed";
      failed = true;
    }
    mbedtls_sha256_free(&hash);
    if (failed) break;
    if (!digest_matches(digest, job->release.sha256)) {
      out.message = "SHA-256 checksum mismatch";
      break;
    }
    out.image = std::move(storage);
    out.ok = true;
    out.message = "Verified";
  } while (false);
  job->finished.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

unsigned FirmwareDownloader::progress_percent() const {
  if (job_ == nullptr) return 0;
  const size_t size = job_->expected.load(std::memory_order_relaxed);
  if (size == 0) return 0;
  return static_cast<unsigned>(100 * job_->received.load(std::memory_order_relaxed) / size);
}

bool FirmwareDownloader::collect(DownloadedFirmware& out) {
  if (job_ == nullptr || !job_->finished.load(std::memory_order_acquire)) return false;
  out = std::move(job_->result);
  delete job_;
  job_ = nullptr;
  return true;
}

}  // namespace esphome::esphome_radio_bridge::firmware
