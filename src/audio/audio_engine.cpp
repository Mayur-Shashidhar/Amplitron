#include "audio/audio_engine.h"
#include "audio/audio_backend.h"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <chrono>

namespace GuitarAmp {

// =============================================================================
// Construction / destruction
// =============================================================================

AudioEngine::AudioEngine() {
    process_buffer_.resize(MAX_BUFFER_SIZE, 0.0f);
    process_buffer_right_.resize(MAX_BUFFER_SIZE, 0.0f);
    backend_ = create_audio_backend();
}

AudioEngine::~AudioEngine() {
    shutdown();
    destroy_audio_backend(backend_);
    backend_ = nullptr;
}

// =============================================================================
// Effect chain management (platform-agnostic)
// =============================================================================

void AudioEngine::add_effect(std::shared_ptr<Effect> effect) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    effect->set_sample_rate(sample_rate_);
    effects_.push_back(std::move(effect));
}

void AudioEngine::insert_effect(int index, std::shared_ptr<Effect> effect) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    effect->set_sample_rate(sample_rate_);
    if (index >= 0 && index < static_cast<int>(effects_.size())) {
        effects_.insert(effects_.begin() + index, std::move(effect));
    } else {
        effects_.push_back(std::move(effect));
    }
}

void AudioEngine::remove_effect(int index) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    if (index >= 0 && index < static_cast<int>(effects_.size())) {
        effects_.erase(effects_.begin() + index);
    }
}

void AudioEngine::restore_effects_state(std::vector<std::shared_ptr<Effect>> new_effects) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    effects_.clear();
    for (auto& fx : new_effects) {
        fx->set_sample_rate(sample_rate_);
        effects_.push_back(std::move(fx));
    }
}

void AudioEngine::set_tuner_tap(std::shared_ptr<Effect> tap) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    tuner_tap_ = std::move(tap);
    if (tuner_tap_) {
        tuner_tap_->set_sample_rate(sample_rate_);
        tuner_tap_->reset();
    }
}

void AudioEngine::clear_tuner_tap() {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    tuner_tap_.reset();
}

bool AudioEngine::has_tuner_tap() const {
    return tuner_tap_ != nullptr;
}

void AudioEngine::move_effect(int from, int to) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    int n = static_cast<int>(effects_.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    auto effect = effects_[from];
    effects_.erase(effects_.begin() + from);
    effects_.insert(effects_.begin() + to, effect);
}

// =============================================================================
// Buffer / sample rate settings (call stop/start which are backend-provided)
// =============================================================================

void AudioEngine::set_buffer_size(int size) {
    size = std::max(MIN_BUFFER_SIZE, std::min(MAX_BUFFER_SIZE, size));
    int prev_size = buffer_size_;
    bool was_running = running_;
    if (was_running) stop();
    buffer_size_ = size;
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed with buffer size " + std::to_string(size) + ". Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            buffer_size_ = prev_size;
            start();
        } else {
            last_error_.clear();
        }
    }
}

void AudioEngine::set_sample_rate(int rate) {
    int prev_rate = sample_rate_;
    bool was_running = running_;
    if (was_running) stop();
    sample_rate_ = rate;
    {
        std::lock_guard<std::mutex> lock(effect_mutex_);
        for (auto& fx : effects_) {
            fx->set_sample_rate(rate);
            fx->reset();
        }
        if (tuner_tap_) {
            tuner_tap_->set_sample_rate(rate);
            tuner_tap_->reset();
        }
    }
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed with sample rate " + std::to_string(rate) + " Hz. Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            sample_rate_ = prev_rate;
            std::lock_guard<std::mutex> lock(effect_mutex_);
            for (auto& fx : effects_) {
                fx->set_sample_rate(prev_rate);
                fx->reset();
            }
            if (tuner_tap_) {
                tuner_tap_->set_sample_rate(prev_rate);
                tuner_tap_->reset();
            }
            start();
        } else {
            last_error_.clear();
        }
    }
}

// =============================================================================
// Audio processing pipeline (called from platform callback)
// =============================================================================

void AudioEngine::process_audio(const float* input, float* output, int frame_count) {
    auto t_start = std::chrono::steady_clock::now();

    // Safety: ensure process buffers are large enough
    if (frame_count > static_cast<int>(process_buffer_.size())) {
        process_buffer_.resize(frame_count, 0.0f);
        process_buffer_right_.resize(frame_count, 0.0f);
    }

    // Single branch: read once per callback, not per sample.
    const bool analyzer_on = analyzer_enabled_.load(std::memory_order_relaxed);

    // Copy input to processing buffer with gain
    float in_gain = input_gain_.load(std::memory_order_relaxed);
    float peak_in = 0.0f;
    if (analyzer_on) {
        // --- Analyzer-active path: also computes RMS, clipping, and ring-buffer capture ---
        float sum_sq_in = 0.0f;
        bool clipped_in = false;
        int cap = analyzer_capture_index_;
        for (int i = 0; i < frame_count; ++i) {
            process_buffer_[i] = input[i] * in_gain;
            float abs_val = std::fabs(process_buffer_[i]);
            if (abs_val > peak_in) peak_in = abs_val;
            if (abs_val >= 1.0f) clipped_in = true;
            sum_sq_in += process_buffer_[i] * process_buffer_[i];
            analyzer_capture_input_[cap] = process_buffer_[i];
            cap = (cap + 1) & ANALYZER_FFT_MASK;
        }
        input_rms_.store(std::sqrt(sum_sq_in / std::max(frame_count, 1)), std::memory_order_relaxed);
        if (clipped_in) input_clipped_.store(true, std::memory_order_release);
        // cap is carried forward for the output loop below
        analyzer_capture_index_ = cap;
    } else {
        // --- Original zero-overhead path: peak metering only ---
        for (int i = 0; i < frame_count; ++i) {
            process_buffer_[i] = input[i] * in_gain;
            float abs_val = std::fabs(process_buffer_[i]);
            if (abs_val > peak_in) peak_in = abs_val;
        }
    }
    input_level_.store(peak_in);

    // Fan mono input out to both channels before the effect chain
    std::memcpy(process_buffer_right_.data(), process_buffer_.data(),
                static_cast<size_t>(frame_count) * sizeof(float));

    // Drain SetInputGain / SetOutputGain commands first — they only touch
    // atomics and are safe to apply without holding effect_mutex_.  This
    // ensures gain updates are never stalled by a structural mutex hold.
    drain_gain_commands();

    // Process through effect chain.
    // Structural mutations (add/remove/move) are performed by the GUI thread
    // under a std::lock_guard on effect_mutex_.  The audio thread uses
    // try_lock: if acquired, it drains effect-targeting commands and refreshes
    // the audio_shadow_effects_ / audio_shadow_tuner_ mirror; if contended it
    // falls through and processes with the previous shadow (at most one
    // callback behind) — eliminating the dry-pass glitch.
    if (effect_mutex_.try_lock()) {
        drain_commands();
        audio_shadow_effects_ = effects_;
        audio_shadow_tuner_   = tuner_tap_;
        effect_mutex_.unlock();
    }

    // Always process with the shadow — never skip processing.
    if (audio_shadow_tuner_ && audio_shadow_tuner_->is_enabled()) {
        audio_shadow_tuner_->process(process_buffer_.data(), frame_count);
        // Re-sync right channel (tuner may have zeroed the buffer)
        std::memcpy(process_buffer_right_.data(), process_buffer_.data(),
                    static_cast<size_t>(frame_count) * sizeof(float));
    }
    for (auto& fx : audio_shadow_effects_) {
        if (fx->is_enabled()) {
            fx->process_stereo(process_buffer_.data(),
                               process_buffer_right_.data(), frame_count);
        }
    }

    // Copy to output with gain — interleaved stereo [L0, R0, L1, R1, ...]
    float out_gain = output_gain_.load(std::memory_order_relaxed);
    float peak_out = 0.0f;
    if (analyzer_on) {
        // --- Analyzer-active path ---
        float sum_sq_out = 0.0f;
        bool clipped_out = false;
        int cap = (analyzer_capture_index_ - frame_count) & ANALYZER_FFT_MASK;
        for (int i = 0; i < frame_count; ++i) {
            float out_l = clamp(process_buffer_[i]       * out_gain, -1.0f, 1.0f);
            float out_r = clamp(process_buffer_right_[i] * out_gain, -1.0f, 1.0f);
            if (std::fabs(out_l) >= 1.0f || std::fabs(out_r) >= 1.0f) clipped_out = true;
            output[i * 2]     = out_l;
            output[i * 2 + 1] = out_r;
            // Store post-gain left channel back for recorder
            process_buffer_[i] = out_l;
            float abs_val = std::fabs(out_l);
            if (abs_val > peak_out) peak_out = abs_val;
            sum_sq_out += out_l * out_l;
            analyzer_capture_output_[cap] = out_l;
            cap = (cap + 1) & ANALYZER_FFT_MASK;
        }
        output_rms_.store(std::sqrt(sum_sq_out / std::max(frame_count, 1)), std::memory_order_relaxed);
        if (clipped_out) output_clipped_.store(true, std::memory_order_release);

        // Publish snapshot at hop cadence (non-blocking)
        analyzer_samples_since_publish_ += frame_count;
        if (analyzer_samples_since_publish_ >= ANALYZER_HOP_SIZE) {
            if (analyzer_mutex_.try_lock()) {
                // Reorder ring buffer into contiguous time-domain snapshot
                const int start = analyzer_capture_index_;
                const int first_chunk = ANALYZER_FFT_SIZE - start;
                std::memcpy(analyzer_snapshot_input_.data(),
                            analyzer_capture_input_.data() + start,
                            static_cast<size_t>(first_chunk) * sizeof(float));
                std::memcpy(analyzer_snapshot_input_.data() + first_chunk,
                            analyzer_capture_input_.data(),
                            static_cast<size_t>(start) * sizeof(float));
                std::memcpy(analyzer_snapshot_output_.data(),
                            analyzer_capture_output_.data() + start,
                            static_cast<size_t>(first_chunk) * sizeof(float));
                std::memcpy(analyzer_snapshot_output_.data() + first_chunk,
                            analyzer_capture_output_.data(),
                            static_cast<size_t>(start) * sizeof(float));
                analyzer_sequence_.fetch_add(1, std::memory_order_release);
                analyzer_samples_since_publish_ = 0;
                analyzer_mutex_.unlock();
            }
        }
    } else {
        // --- Zero-overhead path ---
        for (int i = 0; i < frame_count; ++i) {
            float out_l = clamp(process_buffer_[i]       * out_gain, -1.0f, 1.0f);
            float out_r = clamp(process_buffer_right_[i] * out_gain, -1.0f, 1.0f);
            output[i * 2]     = out_l;
            output[i * 2 + 1] = out_r;
            // Store post-gain left channel back for recorder
            process_buffer_[i] = out_l;
            float abs_val = std::fabs(out_l);
            if (abs_val > peak_out) peak_out = abs_val;
        }
    }
    output_level_.store(peak_out);

    // Record the processed output — mono left channel
    if (recorder_.is_recording()) {
        recorder_.write_samples(process_buffer_.data(), frame_count);
    }

    // CPU load measurement
    auto t_end = std::chrono::steady_clock::now();
    float duration_us = std::chrono::duration<float, std::micro>(t_end - t_start).count();
    callback_duration_us_.store(duration_us, std::memory_order_relaxed);
    float budget_us = (static_cast<float>(frame_count) / sample_rate_) * 1e6f;
    cpu_load_.store(duration_us / budget_us, std::memory_order_relaxed);
}

// =============================================================================
// Lock-free command drain (called from audio thread)
// =============================================================================

// Drains only SetInputGain / SetOutputGain commands that sit at the front of
// the queue.  Stops as soon as it encounters an effect-targeting command so
// FIFO order relative to SetEffectParam/etc. is preserved.  May be called
// without holding effect_mutex_.
void AudioEngine::drain_gain_commands() {
    AudioCommand cmd;
    while (command_queue_.try_peek(cmd)) {
        if (cmd.type == AudioCommand::SetInputGain) {
            command_queue_.try_pop(cmd);
            input_gain_.store(cmd.value, std::memory_order_relaxed);
        } else if (cmd.type == AudioCommand::SetOutputGain) {
            command_queue_.try_pop(cmd);
            output_gain_.store(cmd.value, std::memory_order_relaxed);
        } else {
            break;  // Front is an effect command — leave it for drain_commands().
        }
    }
}

void AudioEngine::drain_commands() {
    AudioCommand cmd;
    while (command_queue_.try_pop(cmd)) {
        switch (cmd.type) {
            case AudioCommand::SetEffectParam:
                if (cmd.effect_index >= 0 &&
                    cmd.effect_index < static_cast<int>(effects_.size())) {
                    auto& params = effects_[cmd.effect_index]->params();
                    if (cmd.param_index >= 0 &&
                        cmd.param_index < static_cast<int>(params.size())) {
                        params[cmd.param_index].value = cmd.value;
                    }
                }
                break;
            case AudioCommand::SetEffectEnabled:
                if (cmd.effect_index >= 0 &&
                    cmd.effect_index < static_cast<int>(effects_.size())) {
                    effects_[cmd.effect_index]->set_enabled(cmd.value > 0.5f);
                }
                break;
            case AudioCommand::SetEffectMix:
                if (cmd.effect_index >= 0 &&
                    cmd.effect_index < static_cast<int>(effects_.size())) {
                    effects_[cmd.effect_index]->set_mix(cmd.value);
                }
                break;
            case AudioCommand::SetInputGain:
                input_gain_.store(cmd.value, std::memory_order_relaxed);
                break;
            case AudioCommand::SetOutputGain:
                output_gain_.store(cmd.value, std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }
}

// =============================================================================
// GUI-thread SPSC push methods
// =============================================================================

void AudioEngine::set_input_gain(float gain) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetInputGain;
    cmd.value = gain;
    command_queue_.try_push(cmd);
    input_gain_.store(gain, std::memory_order_relaxed);  // immediate read-back for GUI
}

void AudioEngine::set_output_gain(float gain) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetOutputGain;
    cmd.value = gain;
    command_queue_.try_push(cmd);
    output_gain_.store(gain, std::memory_order_relaxed);
}

void AudioEngine::push_param_change(int effect_index, int param_index, float value) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetEffectParam;
    cmd.effect_index = effect_index;
    cmd.param_index = param_index;
    cmd.value = value;
    command_queue_.try_push(cmd);
}

void AudioEngine::push_effect_enabled(int effect_index, float enabled) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetEffectEnabled;
    cmd.effect_index = effect_index;
    cmd.value = enabled;
    command_queue_.try_push(cmd);
}

void AudioEngine::push_effect_mix(int effect_index, float mix) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetEffectMix;
    cmd.effect_index = effect_index;
    cmd.value = mix;
    command_queue_.try_push(cmd);
}

// =============================================================================
// Buffer auto-tuning
// =============================================================================

int AudioEngine::get_suggested_buffer_size() const {
    float load = cpu_load_.load(std::memory_order_relaxed);
    int current = buffer_size_;

    // If consistently overloaded (>80%), suggest next larger buffer
    if (load > 0.80f) {
        if (current < MAX_BUFFER_SIZE) {
            return std::min(current * 2, MAX_BUFFER_SIZE);
        }
    }
    // If consistently underloaded (<30%), suggest next smaller buffer
    if (load < 0.30f) {
        if (current > MIN_BUFFER_SIZE) {
            return std::max(current / 2, MIN_BUFFER_SIZE);
        }
    }
    return current;  // no change
}

bool AudioEngine::copy_analyzer_snapshot(float* input_dest,
                                         float* output_dest,
                                         int sample_count) const {
    if (!input_dest || !output_dest || sample_count <= 0) {
        return false;
    }

    const int count = std::min(sample_count, ANALYZER_FFT_SIZE);
    std::lock_guard<std::mutex> lock(analyzer_mutex_);
    const uint64_t seq = analyzer_sequence_.load(std::memory_order_relaxed);
    if (seq == 0) {
        return false;
    }

    std::memcpy(input_dest, analyzer_snapshot_input_.data(), static_cast<size_t>(count) * sizeof(float));
    std::memcpy(output_dest, analyzer_snapshot_output_.data(), static_cast<size_t>(count) * sizeof(float));
    return true;
}

} // namespace GuitarAmp
