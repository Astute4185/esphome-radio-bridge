#pragma once

// Radio-agnostic firmware metadata. Transport, JSON and tasking live in the .cpp.
#include <cstddef>
#include <string>
#include <vector>

namespace esphome::http_request {
class HttpRequestComponent;
}

namespace esphome::esphome_radio_bridge::firmware {

inline bool is_https_url(const std::string& url) {
  constexpr char PREFIX[] = "https://";
  if (url.compare(0, sizeof(PREFIX) - 1, PREFIX) != 0 || url.size() > 1024 ||
      url.size() <= sizeof(PREFIX) - 1)
    return false;
  const size_t host_end = url.find_first_of("/?#", sizeof(PREFIX) - 1);
  const size_t host_length = (host_end == std::string::npos ? url.size() : host_end) - (sizeof(PREFIX) - 1);
  if (host_length == 0) return false;
  for (char c : url) {
    if (static_cast<unsigned char>(c) <= 32 || static_cast<unsigned char>(c) >= 127 || c == '\\' || c == '@')
      return false;
  }
  return url.find('#') == std::string::npos;
}

// Resolve catalogue entries without assuming where the catalogue lives in the repository.
// An unprefixed entry may include directory segments already present in the catalogue URL
// (e.g. "firmware/..."); the longest complete segment overlap is used once. A leading
// "./" explicitly means catalogue-directory-relative, even if directories repeat.
// Entries cannot change the HTTPS origin or traverse into parent directories.
inline bool resolve_manifest_url(const std::string& catalogue_url, const std::string& path,
                                 std::string& resolved) {
  if (!is_https_url(catalogue_url) || path.empty() || path.front() == '/' || path.size() > 512 ||
      path.find("..") != std::string::npos || path.find(":") != std::string::npos ||
      path.find('?') != std::string::npos || path.find('#') != std::string::npos ||
      path.find('\\') != std::string::npos || path.find('%') != std::string::npos)
    return false;
  for (char c : path) {
    if (static_cast<unsigned char>(c) <= 32 || static_cast<unsigned char>(c) >= 127) return false;
  }
  const auto slash = catalogue_url.rfind('/');
  if (slash == std::string::npos || slash < sizeof("https://") - 1) return false;
  const std::string directory = catalogue_url.substr(0, slash + 1);

  if (path.compare(0, 2, "./") == 0) {
    if (path.size() == 2) return false;
    resolved = directory + path.substr(2);
  } else {
    size_t overlap = 0;
    // Match only whole path segments. This works for raw GitHub URLs, GitHub
    // branch/tag paths and other HTTPS hosting without knowing their structure.
    auto separator = directory.find('/', sizeof("https://") - 1);
    while (separator != std::string::npos) {
      const std::string suffix = directory.substr(separator + 1);
      if (!suffix.empty() && suffix.size() > overlap && path.compare(0, suffix.size(), suffix) == 0)
        overlap = suffix.size();
      separator = directory.find('/', separator + 1);
    }
    resolved = directory + path.substr(overlap);
  }
  return is_https_url(resolved);
}

inline bool is_github_release_url(const std::string& url) {
  return is_https_url(url) && url.compare(0, 19, "https://github.com/") == 0 &&
         url.find("/releases/download/") != std::string::npos;
}

inline bool valid_firmware_version(const std::string& version) {
  if (version.empty() || version.size() > 32 || version.front() == '.' || version.back() == '.') return false;
  bool previous_dot = false;
  for (char c : version) {
    if (c == '.') {
      if (previous_dot) return false;
      previous_dot = true;
    } else if (c >= '0' && c <= '9') {
      previous_dot = false;
    } else {
      return false;
    }
  }
  return true;
}

inline bool valid_sha256(const std::string& digest) {
  if (digest.size() != 64) return false;
  for (char c : digest) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

struct FirmwareRelease {
  std::string firmware_version;
  std::string radio_version;
  std::string release_type;
  std::string sha256;
  std::string file;
};

struct FirmwareLoadResult {
  bool ok{false};
  std::string message;
  std::string manifest_url;
  std::string board;
  std::string radio_type;
  std::string latest;
  std::vector<FirmwareRelease> versions;
};

// Data validation is independent of JSON and HTTP and can be unit-tested natively.
inline const char* release_validation_error(const FirmwareRelease& release) {
  if (!valid_firmware_version(release.firmware_version)) return "Invalid firmware_version";
  if (release.radio_version.empty() || release.radio_version.size() > 64) return "Invalid radio_version";
  if (!valid_sha256(release.sha256)) return "Invalid sha256 (expected 64 hexadecimal characters)";
  if (!is_github_release_url(release.file)) return "Invalid file (expected HTTPS GitHub Release URL)";
  if (release.release_type != "beta" && release.release_type != "candidate" &&
      release.release_type != "release")
    return "Invalid release_type (expected beta, candidate or release)";
  return nullptr;
}

inline bool valid_release(const FirmwareRelease& release) {
  return release_validation_error(release) == nullptr;
}

inline const char* manifest_validation_error(const FirmwareLoadResult& metadata) {
  if (metadata.latest.empty() != metadata.versions.empty())
    return "Manifest latest and versions are inconsistent";
  if (!metadata.latest.empty() && !valid_firmware_version(metadata.latest))
    return "Invalid latest firmware version";
  bool latest_found = metadata.latest.empty();
  for (size_t i = 0; i < metadata.versions.size(); ++i) {
    const auto& release = metadata.versions[i];
    if (const char* reason = release_validation_error(release); reason != nullptr) return reason;
    if (release.firmware_version == metadata.latest) latest_found = true;
    for (size_t j = 0; j < i; ++j) {
      if (metadata.versions[j].firmware_version == release.firmware_version)
        return "Duplicate firmware_version in versions";
    }
  }
  if (!latest_found) return "Manifest latest does not match a published firmware_version";
  return nullptr;
}

inline bool valid_manifest_metadata(const FirmwareLoadResult& metadata) {
  return manifest_validation_error(metadata) == nullptr;
}

// Worker task never touches the UART or any ESPHome entity; the main loop owns published state.
class FirmwareCatalogue {
 public:
  bool start(http_request::HttpRequestComponent* request, const std::string& catalogue_url,
             const std::string& manifest_override, const std::string& board, const std::string& radio_type);
  bool collect(FirmwareLoadResult& out);
  bool busy() const { return job_ != nullptr; }

 private:
  struct Job;
  static void worker_(void* param);
  Job* job_{nullptr};
};

}  // namespace esphome::esphome_radio_bridge::firmware
