#pragma once

#include "common.h"
#include "audio/audio_engine.h"
#include "midi/midi_manager.h"
#include <fstream>
#include <map>
#include <sstream>

namespace Amplitron {

struct PresetData {
    std::string name;
    std::string description;
    float input_gain = 0.7f;
    float output_gain = 0.8f;

    struct EffectData {
        std::string type;
        bool enabled = false;
        float mix = 1.0f;
        std::vector<std::pair<std::string, float>> params;
        std::map<std::string, std::string> metadata;  // string key-value pairs (e.g. IR file path)
    };
    std::vector<EffectData> effects;
    std::vector<MidiMapping> midi_mappings;
};

class PresetManager {
public:
    // Save provided preset data to JSON file
    static bool save_preset_data(const std::string& filepath,
                                 const PresetData& preset);

    // Save current engine state to JSON file
    static bool save_preset(const std::string& filepath,
                            const std::string& preset_name,
                            const std::string& description,
                            AudioEngine& engine,
                            const std::vector<MidiMapping>& midi_mappings = {});

    // Load preset from JSON file and apply to engine
    static bool load_preset(const std::string& filepath,
                            AudioEngine& engine,
                            MidiManager* midi_manager = nullptr);

    // Get the active presets directory (creates if needed).
    // Priority: custom user dir → system default dir → local "presets" fallback.
    static std::string get_presets_dir();

    // Override the presets directory with a user-selected path.
    // Pass empty string to revert to auto-detection.
    // Automatically populates the new directory with factory presets if empty.
    static void set_presets_dir(const std::string& dir);

    // Return the currently configured custom directory (empty = auto).
    static const std::string& custom_presets_dir() { return custom_presets_dir_; }

    // Persist / restore the custom directory to a config file.
    static void save_config();
    static void load_config();

    // List available preset files in the presets directory
    static std::vector<std::string> list_presets();

    // Get last error message
    static const std::string& last_error() { return last_error_; }

private:
    static std::string last_error_;
    static std::string custom_presets_dir_;

    // Create and save factory presets to the given directory
    static void save_factory_presets(const std::string& dir);

    // Return platform config file path (e.g. ~/.config/amplitron/config.json)
    static std::string get_config_path();

    // Return platform system-wide presets path (e.g. /usr/share/amplitron/presets)
    static std::string get_system_presets_dir();


};

} // namespace Amplitron
