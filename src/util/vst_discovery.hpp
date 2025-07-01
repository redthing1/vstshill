#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace vstk {
namespace util {

// plugin discovery results
struct PluginInfo {
  std::string path;
  std::string name;
  std::filesystem::file_time_type last_modified;
  uintmax_t file_size;
  bool is_valid_bundle;

  PluginInfo() : file_size(0), is_valid_bundle(false) {}
};

// cross-platform vst3 directory discovery
std::vector<std::string> get_vst3_search_paths();

// comprehensive plugin discovery with metadata
std::vector<PluginInfo>
discover_vst3_plugins(const std::vector<std::string>& search_paths = {});

// lightweight plugin path discovery
std::vector<std::string>
find_vst3_plugins(const std::vector<std::string>& search_paths = {});

// validates vst3 bundle structure
bool is_valid_vst3_bundle(const std::filesystem::path& path);

// directory scanning utilities
std::vector<PluginInfo> scan_directory(const std::filesystem::path& directory,
                                       bool recursive = true);
bool is_directory_accessible(const std::filesystem::path& path);

// plugin resolution utilities
std::string resolve_plugin_path(const std::string& input);

} // namespace util
} // namespace vstk