#include "vst_discovery.hpp"
#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
#include <knownfolders.h>
#include <shlobj.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace vstk {
namespace util {

std::vector<std::string> get_vst3_search_paths() {
  std::vector<std::string> paths;

#ifdef __APPLE__
  // system-wide plugins
  paths.push_back("/Library/Audio/Plug-Ins/VST3");

  // user plugins
  if (const char* home = std::getenv("HOME")) {
    paths.push_back(std::string(home) + "/Library/Audio/Plug-Ins/VST3");
  }

#elif defined(_WIN32)
  // system-wide plugins (Program Files)
  if (const char* pf = std::getenv("PROGRAMFILES")) {
    paths.push_back(std::string(pf) + "\\Common Files\\VST3");
  } else {
    paths.push_back("C:\\Program Files\\Common Files\\VST3");
  }

  // system-wide plugins (Program Files x86)
  if (const char* pf86 = std::getenv("PROGRAMFILES(X86)")) {
    paths.push_back(std::string(pf86) + "\\Common Files\\VST3");
  }

  // user plugins
  if (const char* appdata = std::getenv("APPDATA")) {
    paths.push_back(std::string(appdata) + "\\VST3");
  }

#else // linux and other unix-like systems
  // user plugins
  if (const char* home = std::getenv("HOME")) {
    paths.push_back(std::string(home) + "/.vst3");
  }

  // system-wide plugins
  paths.push_back("/usr/lib/vst3");
  paths.push_back("/usr/local/lib/vst3");

  // additional standard locations
  if (const char* xdg_data_home = std::getenv("XDG_DATA_HOME")) {
    paths.push_back(std::string(xdg_data_home) + "/vst3");
  }

  if (const char* home = std::getenv("HOME")) {
    paths.push_back(std::string(home) + "/.local/share/vst3");
  }
#endif

  return paths;
}

bool is_directory_accessible(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec) &&
         std::filesystem::is_directory(path, ec) && !ec;
}

bool is_valid_vst3_bundle(const std::filesystem::path& path) {
  std::error_code ec;

  if (!std::filesystem::is_directory(path, ec) || ec ||
      path.extension() != ".vst3") {
    return false;
  }

  auto contents_path = path / "Contents";
  if (!std::filesystem::exists(contents_path, ec) || ec ||
      !std::filesystem::is_directory(contents_path, ec) || ec) {
    return false;
  }

  // check for platform-specific binary directory
#ifdef __APPLE__
  auto binary_path = contents_path / "MacOS";
#elif defined(_WIN32)
  auto binary_path = contents_path / "x86_64-win";
#else
  auto binary_path = contents_path / "x86_64-linux";
#endif

  return std::filesystem::exists(binary_path, ec) && !ec &&
         std::filesystem::is_directory(binary_path, ec) && !ec;
}

std::vector<PluginInfo> scan_directory(const std::filesystem::path& directory,
                                       bool recursive) {
  std::vector<PluginInfo> plugins;

  if (!is_directory_accessible(directory)) {
    return plugins;
  }

  auto process_entry = [&](const std::filesystem::directory_entry& entry) {
    std::error_code ec;
    if (entry.is_directory(ec) && !ec && entry.path().extension() == ".vst3") {
      PluginInfo info;
      info.path = entry.path().string();
      info.name = entry.path().stem().string();
      info.is_valid_bundle = is_valid_vst3_bundle(entry.path());

      info.last_modified = entry.last_write_time(ec);
      if (ec) {
        info.last_modified = std::filesystem::file_time_type::min();
      }

      info.file_size = std::filesystem::file_size(entry.path(), ec);
      if (ec) {
        info.file_size = 0;
      }

      plugins.push_back(std::move(info));
    }
  };

  try {
    if (recursive) {
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator(directory)) {
        process_entry(entry);
      }
    } else {
      for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        process_entry(entry);
      }
    }
  } catch (const std::filesystem::filesystem_error&) {
    // silently ignore inaccessible directories
  } catch (const std::exception&) {
    // silently ignore other errors
  }

  return plugins;
}

std::vector<PluginInfo>
discover_vst3_plugins(const std::vector<std::string>& search_paths) {
  std::vector<PluginInfo> plugins;
  auto paths = search_paths.empty() ? get_vst3_search_paths() : search_paths;

  for (const auto& path_str : paths) {
    std::filesystem::path path(path_str);
    auto path_plugins = scan_directory(path, true);
    plugins.insert(plugins.end(), std::make_move_iterator(path_plugins.begin()),
                   std::make_move_iterator(path_plugins.end()));
  }

  // sort by name for consistent results
  std::sort(
      plugins.begin(), plugins.end(),
      [](const PluginInfo& a, const PluginInfo& b) { return a.name < b.name; });

  return plugins;
}

std::vector<std::string>
find_vst3_plugins(const std::vector<std::string>& search_paths) {
  auto discovery_results = discover_vst3_plugins(search_paths);
  std::vector<std::string> plugin_paths;
  plugin_paths.reserve(discovery_results.size());

  for (const auto& info : discovery_results) {
    plugin_paths.push_back(info.path);
  }

  return plugin_paths;
}

} // namespace util
} // namespace vstk