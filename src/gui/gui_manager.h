#pragma once

#include "common.h"
#include "audio/audio_engine.h"
#include "preset_manager.h"
#include "gui/command_history.h"
#include "gui/spectrum_analyzer.h"
#include "audio/effects/tuner.h"
#include <thread>
#include <mutex>
#include <string>

struct SDL_Window;
typedef void* SDL_GLContext;

namespace GuitarAmp {

class PedalBoard;

/**
 * @brief Top-level GUI controller.
 *
 * Owns the SDL window, OpenGL context, Dear ImGui state, and the
 * PedalBoard / CommandHistory instances. Drives the main render loop
 * and dispatches keyboard shortcuts.
 */
class GuiManager {
public:
    /**
     * @brief Construct the GUI manager.
     * @param engine Reference to the audio engine shared with PedalBoard.
     */
    GuiManager(AudioEngine& engine);

    /** @brief Destructor — calls shutdown() if still initialized. */
    ~GuiManager();

    /**
     * @brief Create the SDL window, OpenGL context, and ImGui backend.
     * @param width  Initial window width in pixels.
     * @param height Initial window height in pixels.
     * @return true on success.
     */
    bool initialize(int width = 1280, int height = 720);

    /** @brief Tear down ImGui, OpenGL, and SDL resources. */
    void shutdown();

    /**
     * @brief Process events, render one frame, and swap buffers.
     * @return false when the application should quit.
     */
    bool run_frame();

private:
    /** @brief Render the main menu bar (File, Edit, View, Help). */
    void render_menu_bar();

    /** @brief Render the audio settings modal window. */
    void render_settings_window();

    /**
     * @brief Render a single vertical level meter.
     * @param label Display label.
     * @param level Current peak level (0.0–1.0).
     * @param x     Screen X position.
     * @param y     Screen Y position.
     * @param w     Width in pixels.
     * @param h     Height in pixels.
     */
    void render_meter(const char* label, float level, float x, float y, float w, float h);

    /** @brief Render input/output gain sliders and level meters. */
    void render_master_controls();

    /** @brief Render the "Save Preset" popup dialog. */
    void render_save_preset_popup();

    /** @brief Render the "Load Preset" popup dialog. */
    void render_load_preset_popup();

    /** @brief Render recording start/stop controls. */
    void render_recording_controls();

    /** @brief Render the "Save Recording" file dialog. */
    void render_recording_save_dialog();

    /** @brief Render the collapsible VU + spectrum analyzer panel. */
    void render_analyzer_panel();

    /** @brief Render the tuner modal dialog. */
    void render_tuner_modal();

    /** @brief Draw a horizontal VU bar with peak hold and clipping flash. */
    void render_vu_bar(const char* id,
                       const char* label,
                       float rms_level,
                       float peak_hold,
                       bool clip_active,
                       float clip_flash,
                       ImU32 base_color,
                       ImU32 peak_color);

    void refresh_presets(bool preserve_selection = true);
    bool save_named_preset(const std::string& preset_name,
                           const std::string& description);
    bool load_preset_by_index(int index);
    bool delete_preset_by_index(int index);
    std::string preset_name_from_path(const std::string& filepath) const;
    std::string preset_path_from_name(const std::string& preset_name) const;
    void ensure_factory_presets();

    AudioEngine& engine_;
    CommandHistory command_history_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    std::unique_ptr<PedalBoard> pedal_board_;

    bool initialized_ = false;
    bool show_settings_ = false;
    bool show_save_preset_ = false;
    bool show_load_preset_ = false;
    bool show_tuner_ = false;
    std::shared_ptr<TunerPedal> tuner_instance_;
    int window_width_ = 1280;
    int window_height_ = 720;

    // Preset UI state
    char preset_name_buf_[128] = "My Preset";
    char preset_desc_buf_[256] = "";
    std::vector<std::string> preset_files_;
    int selected_preset_index_ = -1;
    bool factory_presets_initialized_ = false;
    bool preset_dialog_is_new_ = false;
    std::string preset_status_msg_;

    // Smoothed meter values
    float smoothed_input_level_ = 0.0f;
    float smoothed_output_level_ = 0.0f;
    float smoothed_input_rms_ = 0.0f;
    float smoothed_output_rms_ = 0.0f;
    float input_rms_peak_hold_ = 0.0f;
    float output_rms_peak_hold_ = 0.0f;
    float input_clip_flash_ = 0.0f;
    float output_clip_flash_ = 0.0f;

    bool analyzer_panel_expanded_ = true;
    SpectrumAnalyzer::DisplayMode analyzer_mode_ = SpectrumAnalyzer::DisplayMode::Output;
    std::unique_ptr<SpectrumAnalyzer> spectrum_analyzer_;
    std::array<float, AudioEngine::ANALYZER_FFT_SIZE> analyzer_input_buf_{};
    std::array<float, AudioEngine::ANALYZER_FFT_SIZE> analyzer_output_buf_{};
    uint64_t analyzer_last_sequence_ = 0;

    // Recording UI state
    bool show_recording_save_ = false;
    bool recording_save_pending_ = false;
    float rec_waveform_buf_[512] = {};

    // Update checking
    void check_for_updates();
    std::thread update_check_thread_;
    std::mutex update_mutex_;
    bool has_new_release_ = false;
    std::string new_release_version_;
    std::string new_release_url_;
};

} // namespace GuitarAmp
