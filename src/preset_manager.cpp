#include "preset_manager.h"
#include "audio/effect_factory.h"

#include <iostream>
#include <ctime>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define MKDIR(path) _mkdir(path)
#define STAT_STRUCT struct _stat
#define STAT_FN     _stat
#elif defined(__APPLE__)
#include <dirent.h>
#include <mach-o/dyld.h>
#define MKDIR(path) mkdir(path, 0755)
#define STAT_STRUCT struct stat
#define STAT_FN     stat
#else
#include <dirent.h>
#define MKDIR(path) mkdir(path, 0755)
#define STAT_STRUCT struct stat
#define STAT_FN     stat
#endif

namespace Amplitron {

// Forward declaration
static void append_json_files(const std::string& dir, std::vector<std::string>& result);

std::string PresetManager::last_error_;
std::string PresetManager::custom_presets_dir_;

static bool dir_exists(const std::string& path) {
    STAT_STRUCT st;
    return STAT_FN(path.c_str(), &st) == 0 &&
#ifdef _WIN32
           (st.st_mode & _S_IFDIR);
#else
           S_ISDIR(st.st_mode);
#endif
}

std::string PresetManager::get_system_presets_dir() {
#ifdef _WIN32
    const char* pd = std::getenv("ProgramData");
    return pd ? std::string(pd) + "\\Amplitron\\presets" : "";
#elif defined(__APPLE__)
    return "/Library/Application Support/Amplitron/presets";
#else
    return "/usr/share/amplitron/presets";
#endif
}

std::string PresetManager::get_config_path() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (!appdata) return "amplitron_config.json";
    std::string dir = std::string(appdata) + "\\Amplitron";
    MKDIR(dir.c_str());
    return dir + "\\config.json";
#else
    const char* home = std::getenv("HOME");
    if (!home) return "amplitron_config.json";
    std::string config_dir = std::string(home) + "/.config/amplitron";
    std::filesystem::create_directories(config_dir);
    return config_dir + "/config.json";
#endif
}

static std::string get_user_presets_dir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (!appdata) return "";
    return std::string(appdata) + "\\Amplitron\\presets";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/Library/Application Support/Amplitron/presets";
#else
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.config/amplitron/presets";
#endif
}

void PresetManager::set_presets_dir(const std::string& dir) {
    if (dir.empty()) {
        custom_presets_dir_ = "";
        return;
    }
    try {
        std::filesystem::create_directories(dir);
    } catch (...) {
        // If creation fails, custom_presets_dir_ is left unchanged
        return;
    }
    if (dir_exists(dir)) {
        custom_presets_dir_ = dir;
        // Populate new directory with factory presets if it's empty
        std::vector<std::string> dir_presets;
        append_json_files(dir, dir_presets);
        if (dir_presets.empty()) {
            save_factory_presets(dir);
        }
    }
    // If creation/validation fails, custom_presets_dir_ is left unchanged.
}

void PresetManager::save_config() {
    std::string path = get_config_path();
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "{\n";
    f << "  \"presets_dir\": \"";
    // Escape backslashes for JSON (important on Windows)
    for (char c : custom_presets_dir_) {
        if (c == '\\') f << "\\\\";
        else if (c == '"') f << "\\\"";
        else f << c;
    }
    f << "\"\n}\n";
}

void PresetManager::load_config() {
    std::string path = get_config_path();
    std::ifstream f(path);
    if (!f.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Extract presets_dir value from minimal JSON
    const std::string key = "\"presets_dir\"";
    size_t key_pos = content.find(key);
    if (key_pos == std::string::npos) return;

    size_t colon = content.find(':', key_pos + key.size());
    if (colon == std::string::npos) return;

    size_t quote_open = content.find('"', colon + 1);
    if (quote_open == std::string::npos) return;

    std::string value;
    size_t i = quote_open + 1;
    while (i < content.size() && content[i] != '"') {
        if (content[i] == '\\' && i + 1 < content.size()) {
            ++i;
            if (content[i] == '\\') value += '\\';
            else if (content[i] == '"') value += '"';
            else if (content[i] == 'n') value += '\n';
            else { value += '\\'; value += content[i]; }
        } else {
            value += content[i];
        }
        ++i;
    }

    if (!value.empty() && dir_exists(value)) {
        custom_presets_dir_ = value;
    }
}

std::string PresetManager::get_presets_dir() {
    // 1. User-selected custom directory — verify it still exists.
    if (!custom_presets_dir_.empty()) {
        MKDIR(custom_presets_dir_.c_str());
        if (dir_exists(custom_presets_dir_)) {
            return custom_presets_dir_;
        }
        // Stale path — fall through to the default user dir.
    }

    // 2. User-writable default presets directory (creates intermediate parents).
    std::string user_dir = get_user_presets_dir();
    if (!user_dir.empty()) {
        std::filesystem::create_directories(user_dir);
        if (dir_exists(user_dir)) {
            return user_dir;
        }
    }

    // 3. Local fallback
    std::string dir = "presets";
    MKDIR(dir.c_str());
    return dir;
}

static void append_json_files(const std::string& dir,
                              std::vector<std::string>& result) {
#ifdef _WIN32
    std::string pattern = dir + "\\*.json";
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern.c_str(), &fd);
    if (handle != -1) {
        do {
            if (!(fd.attrib & _A_SUBDIR)) {
                result.push_back(dir + "\\" + fd.name);
            }
        } while (_findnext(handle, &fd) == 0);
        _findclose(handle);
    }
#else
    DIR* d = opendir(dir.c_str());
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
                result.push_back(dir + "/" + name);
            }
        }
        closedir(d);
    }
#endif
}

static std::string get_bundled_presets_dir() {
#ifdef _WIN32
    // Executable directory + /presets
    char path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, path, sizeof(path))) {
        std::string exe_dir = path;
        size_t last_slash = exe_dir.find_last_of("\\");
        if (last_slash != std::string::npos) {
            return exe_dir.substr(0, last_slash) + "\\presets";
        }
    }
    return "presets";
#elif defined(__APPLE__)
    // Check both app bundle resources and executable directory
    // App bundle: ../Resources/presets relative to executable
    char exe_path[4096];
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        std::string exe_str = exe_path;
        // Try Resources/presets first (app bundle)
        size_t last_slash = exe_str.find_last_of("/");
        if (last_slash != std::string::npos) {
            std::string bundle_presets = exe_str.substr(0, last_slash) + "/../Resources/presets";
            if (dir_exists(bundle_presets)) {
                return bundle_presets;
            }
        }
    }
    // Fall back to executable directory
    return "presets";
#else
    // Check next to executable first
    if (dir_exists("presets")) {
        return "presets";
    }
    // Fall back to system data directory
    return "/usr/share/amplitron/presets";
#endif
}

void PresetManager::save_factory_presets(const std::string& dir) {
    std::string src_dir = get_bundled_presets_dir();
    if (!dir_exists(src_dir)) {
        std::cerr << "Bundled presets directory not found: " << src_dir << std::endl;
        return;
    }

    std::vector<std::string> preset_files;
    append_json_files(src_dir, preset_files);

    for (const auto& src_path : preset_files) {
        // Extract filename from source path
        size_t last_slash = src_path.find_last_of("/\\");
        std::string filename = (last_slash != std::string::npos) ?
                                src_path.substr(last_slash + 1) : src_path;

#ifdef _WIN32
        std::string dest_path = dir + "\\" + filename;
#else
        std::string dest_path = dir + "/" + filename;
#endif

        // Read source file
        std::ifstream src_file(src_path);
        if (!src_file.is_open()) {
            std::cerr << "Could not open source preset: " << src_path << std::endl;
            continue;
        }

        std::string content((std::istreambuf_iterator<char>(src_file)),
                           std::istreambuf_iterator<char>());
        src_file.close();

        // Write to destination
        std::ofstream dest_file(dest_path);
        if (!dest_file.is_open()) {
            std::cerr << "Could not write preset: " << dest_path << std::endl;
            continue;
        }

        dest_file << content;
        dest_file.close();
    }
}

std::vector<std::string> PresetManager::list_presets() {
    std::vector<std::string> result;

    // User / custom presets (writable)
    append_json_files(get_presets_dir(), result);

    // System presets (read-only source — listed for display but not for saves)
    std::string sys_dir = get_system_presets_dir();
    std::string user_dir = get_presets_dir();
    if (!sys_dir.empty() && dir_exists(sys_dir) && sys_dir != user_dir) {
        append_json_files(sys_dir, result);
    }

    return result;
}

std::string PresetManager::escape_json_string(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

std::string PresetManager::unescape_json_string(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '"':  out += '"'; ++i; break;
                case '\\': out += '\\'; ++i; break;
                case 'n':  out += '\n'; ++i; break;
                case 'r':  out += '\r'; ++i; break;
                case 't':  out += '\t'; ++i; break;
                default:   out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string PresetManager::to_json(const PresetData& preset) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"format_version\": 1,\n";
    ss << "  \"name\": \"" << escape_json_string(preset.name) << "\",\n";
    ss << "  \"description\": \"" << escape_json_string(preset.description) << "\",\n";

    // Timestamp
    std::time_t now = std::time(nullptr);
    char timebuf[64];
    std::tm time_info;
#ifdef _WIN32
    localtime_s(&time_info, &now);
#else
    localtime_r(&now, &time_info);
#endif
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", &time_info);
    ss << "  \"saved_at\": \"" << timebuf << "\",\n";

    ss << "  \"input_gain\": " << preset.input_gain << ",\n";
    ss << "  \"output_gain\": " << preset.output_gain << ",\n";
    ss << "  \"effects\": [\n";

    for (size_t e = 0; e < preset.effects.size(); ++e) {
        const auto& fx = preset.effects[e];
        ss << "    {\n";
        ss << "      \"type\": \"" << escape_json_string(fx.type) << "\",\n";
        ss << "      \"enabled\": " << (fx.enabled ? "true" : "false") << ",\n";
        ss << "      \"mix\": " << fx.mix << ",\n";
        ss << "      \"params\": {\n";

        for (size_t p = 0; p < fx.params.size(); ++p) {
            ss << "        \"" << escape_json_string(fx.params[p].first) << "\": "
               << fx.params[p].second;
            if (p + 1 < fx.params.size()) ss << ",";
            ss << "\n";
        }

        ss << "      }\n";
        ss << "    }";
        if (e + 1 < preset.effects.size()) ss << ",";
        ss << "\n";
    }

    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

// Minimal JSON parser helpers
static std::string extract_string_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end = pos + 1;
    while (end < json.size() && !(json[end] == '"' && json[end - 1] != '\\')) ++end;
    return json.substr(pos + 1, end - pos - 1);
}

static float extract_float_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0.0f;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0.0f;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    return std::stof(json.substr(pos));
}

static bool extract_bool_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    return json.find("true", pos) < json.find("false", pos);
}

bool PresetManager::from_json(const std::string& json, PresetData& preset) {
    preset.name = unescape_json_string(extract_string_value(json, "name"));
    preset.description = unescape_json_string(extract_string_value(json, "description"));
    preset.input_gain = extract_float_value(json, "input_gain");
    preset.output_gain = extract_float_value(json, "output_gain");

    // Parse effects array
    size_t effects_pos = json.find("\"effects\"");
    if (effects_pos == std::string::npos) return true;

    size_t arr_start = json.find('[', effects_pos);
    if (arr_start == std::string::npos) return true;

    // Find each effect object {...}
    size_t search_pos = arr_start;
    while (true) {
        size_t obj_start = json.find('{', search_pos + 1);
        if (obj_start == std::string::npos) break;

        // Find matching closing brace (handle nested braces for params)
        int depth = 0;
        size_t obj_end = obj_start;
        for (size_t i = obj_start; i < json.size(); ++i) {
            if (json[i] == '{') ++depth;
            if (json[i] == '}') {
                --depth;
                if (depth == 0) { obj_end = i; break; }
            }
        }
        if (obj_end <= obj_start) break;

        std::string obj = json.substr(obj_start, obj_end - obj_start + 1);

        PresetData::EffectData fx;
        fx.type = unescape_json_string(extract_string_value(obj, "type"));
        fx.enabled = extract_bool_value(obj, "enabled");
        fx.mix = extract_float_value(obj, "mix");

        // Parse params sub-object
        size_t params_pos = obj.find("\"params\"");
        if (params_pos != std::string::npos) {
            size_t p_start = obj.find('{', params_pos);
            size_t p_end = obj.find('}', p_start + 1);
            if (p_start != std::string::npos && p_end != std::string::npos) {
                std::string params_str = obj.substr(p_start + 1, p_end - p_start - 1);

                // Parse key: value pairs
                size_t pos = 0;
                while (pos < params_str.size()) {
                    size_t key_start = params_str.find('"', pos);
                    if (key_start == std::string::npos) break;
                    size_t key_end = params_str.find('"', key_start + 1);
                    if (key_end == std::string::npos) break;

                    std::string pkey = params_str.substr(key_start + 1, key_end - key_start - 1);

                    size_t colon = params_str.find(':', key_end);
                    if (colon == std::string::npos) break;

                    size_t val_start = colon + 1;
                    while (val_start < params_str.size() &&
                           (params_str[val_start] == ' ' || params_str[val_start] == '\t'))
                        ++val_start;

                    try {
                        float val = std::stof(params_str.substr(val_start));
                        fx.params.push_back({pkey, val});
                    } catch (...) {}

                    pos = params_str.find(',', val_start);
                    if (pos == std::string::npos) break;
                    ++pos;
                }
            }
        }

        if (!fx.type.empty()) {
            preset.effects.push_back(fx);
        }

        search_pos = obj_end;
    }

    return true;
}

bool PresetManager::save_preset_data(const std::string& filepath,
                                     const PresetData& preset) {
    std::string json = to_json(preset);

    std::ofstream file(filepath);
    if (!file.is_open()) {
        last_error_ = "Could not open file for writing: " + filepath;
        std::cerr << last_error_ << std::endl;
        return false;
    }

    file << json;
    file.close();

    std::cout << "Preset saved: " << filepath << std::endl;
    return true;
}

bool PresetManager::save_preset(const std::string& filepath,
                                 const std::string& preset_name,
                                 const std::string& description,
                                 AudioEngine& engine) {
    PresetData preset;
    preset.name = preset_name;
    preset.description = description;
    preset.input_gain = engine.get_input_gain();
    preset.output_gain = engine.get_output_gain();

    for (auto& fx : engine.effects()) {
        PresetData::EffectData fd;
        fd.type = fx->name();
        fd.enabled = fx->is_enabled();
        fd.mix = fx->get_mix();
        for (auto& p : fx->params()) {
            fd.params.push_back({p.name, p.value});
        }
        preset.effects.push_back(fd);
    }

    return save_preset_data(filepath, preset);
}

bool PresetManager::load_preset(const std::string& filepath,
                                 AudioEngine& engine) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        last_error_ = "Could not open file: " + filepath;
        std::cerr << last_error_ << std::endl;
        return false;
    }

    std::string json((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    file.close();

    PresetData preset;
    if (!from_json(json, preset)) {
        last_error_ = "Failed to parse preset file: " + filepath;
        std::cerr << last_error_ << std::endl;
        return false;
    }

    // Clear current effects
    auto& effects = engine.effects();
    while (!effects.empty()) {
        engine.remove_effect(static_cast<int>(effects.size()) - 1);
    }

    // Apply gains
    engine.set_input_gain(preset.input_gain);
    engine.set_output_gain(preset.output_gain);

    // Recreate effect chain using the factory registry
    for (auto& fd : preset.effects) {
        auto fx = EffectFactory::instance().create(fd.type);
        if (!fx) {
            std::cerr << "Unknown effect type: " << fd.type << std::endl;
            continue;
        }

        fx->set_enabled(fd.enabled);
        fx->set_mix(fd.mix);

        // Apply saved parameter values
        auto& fxparams = fx->params();
        for (auto& saved_param : fd.params) {
            for (auto& ep : fxparams) {
                if (ep.name == saved_param.first) {
                    ep.value = clamp(saved_param.second, ep.min_val, ep.max_val);
                    break;
                }
            }
        }

        engine.add_effect(fx);
    }

    std::cout << "Preset loaded: " << preset.name << " (" << filepath << ")" << std::endl;
    return true;
}

} // namespace Amplitron
