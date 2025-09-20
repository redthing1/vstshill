#include "vst_discovery.hpp"
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <redlog.hpp>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

extern redlog::logger log_main;

namespace vstk {
namespace util {

std::vector<std::string> get_vst3_search_paths() {
  std::vector<std::string> paths;

  log_main.dbg("getting vst3 search paths");

#ifdef __APPLE__
  // system-wide plugins
  paths.push_back("/Library/Audio/Plug-Ins/VST3");

  // user plugins
  if (const char* home = std::getenv("HOME")) {
    paths.push_back(std::string(home) + "/Library/Audio/Plug-Ins/VST3");
  }

#elif defined(_WIN32)
  // system-wide plugins (program files)
  if (const char* pf = std::getenv("PROGRAMFILES")) {
    paths.push_back(std::string(pf) + "\\Common Files\\VST3");
  } else {
    paths.push_back("C:\\Program Files\\Common Files\\VST3");
  }

  // system-wide plugins (program files x86)
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

  log_main.dbg("found search paths", redlog::field("count", paths.size()));
  for (const auto& path : paths) {
    log_main.dbg("search path", redlog::field("path", path));
  }

  return paths;
}

bool is_directory_accessible(const std::filesystem::path& path) {
  std::error_code ec;
  bool exists = std::filesystem::exists(path, ec);
  bool is_dir = exists && std::filesystem::is_directory(path, ec);
  bool accessible = exists && is_dir && !ec;

  log_main.dbg(
      "checking directory accessibility", redlog::field("path", path.string()),
      redlog::field("exists", exists), redlog::field("is_directory", is_dir),
      redlog::field("accessible", accessible));

  if (ec) {
    log_main.dbg("directory check error", redlog::field("path", path.string()),
                 redlog::field("error", ec.message()));
  }

  return accessible;
}

bool is_valid_vst3_bundle(const std::filesystem::path& path) {
  std::error_code ec;

  log_main.dbg("validating vst3 bundle", redlog::field("path", path.string()));

  if (!std::filesystem::is_directory(path, ec) || ec ||
      path.extension() != ".vst3") {
    log_main.dbg("bundle validation failed: not a .vst3 directory",
                 redlog::field("path", path.string()),
                 redlog::field("is_directory",
                               !ec && std::filesystem::is_directory(path, ec)),
                 redlog::field("extension", path.extension().string()));
    return false;
  }

  auto contents_path = path / "Contents";
  if (!std::filesystem::exists(contents_path, ec) || ec ||
      !std::filesystem::is_directory(contents_path, ec) || ec) {
    log_main.dbg(
        "bundle validation failed: no Contents directory",
        redlog::field("path", path.string()),
        redlog::field("contents_path", contents_path.string()),
        redlog::field("contents_exists",
                      !ec && std::filesystem::exists(contents_path, ec)));
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

  bool valid = std::filesystem::exists(binary_path, ec) && !ec &&
               std::filesystem::is_directory(binary_path, ec) && !ec;

  log_main.dbg("bundle validation result", redlog::field("path", path.string()),
               redlog::field("binary_path", binary_path.string()),
               redlog::field("binary_exists",
                             !ec && std::filesystem::exists(binary_path, ec)),
               redlog::field("valid", valid));

  return valid;
}

std::vector<PluginInfo> scan_directory(const std::filesystem::path& directory,
                                       bool recursive) {
  std::vector<PluginInfo> plugins;

  log_main.dbg("scanning directory", redlog::field("path", directory.string()),
               redlog::field("recursive", recursive));

  if (!is_directory_accessible(directory)) {
    log_main.dbg("directory not accessible, skipping",
                 redlog::field("path", directory.string()));
    return plugins;
  }

  auto process_entry = [&](const std::filesystem::directory_entry& entry) {
    std::error_code ec;

    log_main.dbg("processing entry",
                 redlog::field("path", entry.path().string()),
                 redlog::field("extension", entry.path().extension().string()));

    // check for vst3 bundle (directory with .vst3 extension)
    if (entry.is_directory(ec) && !ec && entry.path().extension() == ".vst3") {
      log_main.dbg("found vst3 bundle directory",
                   redlog::field("path", entry.path().string()));

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

      log_main.dbg("added vst3 bundle", redlog::field("name", info.name),
                   redlog::field("path", info.path),
                   redlog::field("valid", info.is_valid_bundle),
                   redlog::field("size", info.file_size));

      plugins.push_back(std::move(info));
    }
#ifdef _WIN32
    // on windows, also check for .vst3 files (single file format)
    else if (entry.is_regular_file(ec) && !ec &&
             entry.path().extension() == ".vst3") {
      log_main.dbg("found vst3 file (windows single file format)",
                   redlog::field("path", entry.path().string()));

      PluginInfo info;
      info.path = entry.path().string();
      info.name = entry.path().stem().string();
      info.is_valid_bundle = true; // single files are considered valid

      info.last_modified = entry.last_write_time(ec);
      if (ec) {
        info.last_modified = std::filesystem::file_time_type::min();
      }

      info.file_size = std::filesystem::file_size(entry.path(), ec);
      if (ec) {
        info.file_size = 0;
      }

      log_main.dbg("added vst3 file", redlog::field("name", info.name),
                   redlog::field("path", info.path),
                   redlog::field("size", info.file_size));

      plugins.push_back(std::move(info));
    }
#endif
  };

  try {
    if (recursive) {
      log_main.dbg("starting recursive directory iteration");
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator(directory)) {
        process_entry(entry);
      }
    } else {
      log_main.dbg("starting non-recursive directory iteration");
      for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        process_entry(entry);
      }
    }
  } catch (const std::filesystem::filesystem_error& e) {
    log_main.dbg("filesystem error during directory scan",
                 redlog::field("path", directory.string()),
                 redlog::field("error", e.what()));
  } catch (const std::exception& e) {
    log_main.dbg("error during directory scan",
                 redlog::field("path", directory.string()),
                 redlog::field("error", e.what()));
  }

  log_main.dbg("scan completed", redlog::field("path", directory.string()),
               redlog::field("plugins_found", plugins.size()));

  return plugins;
}

std::vector<PluginInfo>
discover_vst3_plugins(const std::vector<std::string>& search_paths) {
  std::vector<PluginInfo> plugins;
  auto paths = search_paths.empty() ? get_vst3_search_paths() : search_paths;

  log_main.dbg("starting vst3 plugin discovery",
               redlog::field("search_path_count", paths.size()));

  for (const auto& path_str : paths) {
    std::filesystem::path path(path_str);
    log_main.dbg("scanning search path", redlog::field("path", path_str));

    auto path_plugins = scan_directory(path, true);
    log_main.dbg("path scan result", redlog::field("path", path_str),
                 redlog::field("plugins_found", path_plugins.size()));

    plugins.insert(plugins.end(), std::make_move_iterator(path_plugins.begin()),
                   std::make_move_iterator(path_plugins.end()));
  }

  // sort by name for consistent results
  std::sort(
      plugins.begin(), plugins.end(),
      [](const PluginInfo& a, const PluginInfo& b) { return a.name < b.name; });

  log_main.dbg("discovery completed",
               redlog::field("total_plugins", plugins.size()));

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

namespace {

std::string to_lower(const std::string& str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}

std::vector<PluginInfo>
find_exact_matches(const std::vector<PluginInfo>& plugins,
                   const std::string& input_lower) {
  std::vector<PluginInfo> matches;
  for (const auto& plugin : plugins) {
    if (to_lower(plugin.name) == input_lower) {
      matches.push_back(plugin);
    }
  }
  return matches;
}

std::vector<PluginInfo>
find_partial_matches(const std::vector<PluginInfo>& plugins,
                     const std::string& input_lower) {
  std::vector<PluginInfo> matches;
  for (const auto& plugin : plugins) {
    if (to_lower(plugin.name).find(input_lower) != std::string::npos) {
      matches.push_back(plugin);
    }
  }
  return matches;
}

void log_multiple_matches(const std::vector<PluginInfo>& matches,
                          const std::string& input) {
  log_main.err("multiple plugins found matching", redlog::field("name", input),
               redlog::field("count", matches.size()));

  log_main.inf("available matches:");
  for (const auto& match : matches) {
    log_main.inf("  plugin", redlog::field("name", match.name),
                 redlog::field("path", match.path));
  }
}

} // anonymous namespace

std::string resolve_plugin_path(const std::string& input) {
  log_main.dbg("resolving plugin path", redlog::field("input", input));

  // try as direct path first
  std::error_code ec;
  if (std::filesystem::exists(input, ec) && !ec) {
    log_main.dbg("input is valid path", redlog::field("path", input));
    return input;
  }

  // search for matching plugin by name
  log_main.dbg("input not a valid path, searching by name");
  auto plugins = discover_vst3_plugins();
  auto input_lower = to_lower(input);

  // try exact match first
  auto matches = find_exact_matches(plugins, input_lower);

  // fallback to partial match
  if (matches.empty()) {
    matches = find_partial_matches(plugins, input_lower);
  }

  if (matches.empty()) {
    log_main.err("no plugins found matching", redlog::field("name", input));
    return "";
  }

  if (matches.size() == 1) {
    log_main.inf("resolved plugin", redlog::field("name", input),
                 redlog::field("path", matches[0].path));
    return matches[0].path;
  }

  log_multiple_matches(matches, input);
  return "";
}

} // namespace util
} // namespace vstk