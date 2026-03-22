#include "audio/recorder.h"
#include "audio/audio_engine.h"
#include <iostream>
#include <sstream>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

namespace GuitarAmp {

static void mkdirs(const std::string& path) {
    std::string current;
    for (char c : path) {
        current += c;
        if (c == '/' || c == '\\') {
            MKDIR(current.c_str());
        }
    }
    MKDIR(path.c_str());
}

Recorder::Recorder() {
    for (auto& v : waveform_buf_) v.store(0.0f);
}

Recorder::~Recorder() {
    if (recording_) stop();
}

std::string Recorder::get_recordings_dir() {
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) {
        std::string dir = std::string(home) + "/Documents/Amplitron/recordings";
        mkdirs(dir);
        return dir;
    }
#elif !defined(_WIN32)
    const char* home = std::getenv("HOME");
    if (home) {
        std::string dir = std::string(home) + "/.local/share/amplitron/recordings";
        mkdirs(dir);
        return dir;
    }
#endif
    std::string dir = "recordings";
    MKDIR(dir.c_str());
    return dir;
}

std::string Recorder::generate_filename() {
    std::time_t now = std::time(nullptr);
    char buf[64];
    std::tm time_info;
#ifdef _WIN32
    localtime_s(&time_info, &now);
#else
    localtime_r(&now, &time_info);
#endif
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &time_info);
    return get_recordings_dir() + "/" + std::string(buf) + ".wav";
}

bool Recorder::start(const std::string& filepath, int sample_rate, int channels) {
    if (recording_) return false;

    filepath_ = filepath;
    sample_rate_ = sample_rate;
    channels_ = channels;
    samples_written_ = 0;
    paused_ = false;
    has_unsaved_ = false;
    pause_duration_ = 0.0f;

    // Reset waveform buffer
    for (auto& v : waveform_buf_) v.store(0.0f);
    waveform_write_pos_ = 0;
    current_peak_ = 0.0f;
    bin_sample_count_ = 0;
    bin_peak_ = 0.0f;
    // ~60 waveform updates per second (at 48kHz, ~800 samples per bin)
    samples_per_bin_ = std::max(1, sample_rate / (WAVEFORM_SIZE * 2));

    // Initialize ring buffer and pre-allocated PCM conversion buffer
    ring_buffer_.resize(RING_BUFFER_SIZE, 0.0f);
    ring_write_pos_ = 0;
    ring_read_pos_ = 0;
    pcm_buffer_.resize(4096);

    file_.open(filepath, std::ios::binary);
    if (!file_.is_open()) {
        std::cerr << "Recorder: failed to open " << filepath << std::endl;
        return false;
    }

    write_wav_header();
    recording_ = true;
    start_time_ = std::chrono::steady_clock::now();

    // Start the disk writer thread (keeps file I/O off the real-time audio thread)
    writer_running_ = true;
    writer_thread_ = std::thread(&Recorder::writer_thread_func, this);

    std::cout << "Recording started: " << filepath << std::endl;
    return true;
}

void Recorder::stop() {
    if (!recording_) return;
    recording_ = false;
    paused_ = false;

    // Stop writer thread and wait for it to drain remaining buffered data
    writer_running_ = false;
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    finalize_wav_header();
    file_.close();

    has_unsaved_ = true;

    float dur = get_duration();
    std::cout << "Recording stopped: " << samples_written_.load() << " samples ("
              << dur << "s) saved to " << filepath_ << std::endl;
}

void Recorder::pause() {
    if (!recording_ || paused_) return;
    paused_ = true;
    pause_start_ = std::chrono::steady_clock::now();
}

void Recorder::resume() {
    if (!recording_ || !paused_) return;
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - pause_start_).count();
    pause_duration_.store(pause_duration_.load() + elapsed);
    paused_ = false;
}

bool Recorder::save_to(const std::string& dest_path) {
    if (!has_unsaved_) return false;
    if (filepath_ == dest_path) {
        has_unsaved_ = false;
        return true;
    }
    // Copy file to destination
    std::ifstream src(filepath_, std::ios::binary);
    std::ofstream dst(dest_path, std::ios::binary);
    if (!src.is_open() || !dst.is_open()) return false;
    dst << src.rdbuf();
    src.close();
    dst.close();
    // Remove temp file
    std::remove(filepath_.c_str());
    // Remove metadata sidecar if exists
    std::string meta = filepath_;
    size_t dot = meta.rfind('.');
    if (dot != std::string::npos) meta = meta.substr(0, dot);
    meta += ".meta.json";
    std::remove(meta.c_str());
    filepath_ = dest_path;
    has_unsaved_ = false;
    return true;
}

void Recorder::discard() {
    if (!has_unsaved_ && !recording_) return;
    if (recording_) stop();
    std::remove(filepath_.c_str());
    // Remove metadata sidecar if exists
    std::string meta = filepath_;
    size_t dot = meta.rfind('.');
    if (dot != std::string::npos) meta = meta.substr(0, dot);
    meta += ".meta.json";
    std::remove(meta.c_str());
    has_unsaved_ = false;
    filepath_.clear();
}

void Recorder::write_samples(const float* buffer, int num_samples) {
    if (!recording_ || paused_) return;

    // Push samples into lock-free ring buffer (real-time safe: no alloc, no I/O)
    int64_t wp = ring_write_pos_.load(std::memory_order_relaxed);
    int64_t rp = ring_read_pos_.load(std::memory_order_acquire);
    int64_t available_space = RING_BUFFER_SIZE - (wp - rp);

    int to_write = num_samples * channels_;
    if (to_write > static_cast<int>(available_space)) {
        to_write = static_cast<int>(available_space);
    }

    for (int i = 0; i < to_write; ++i) {
        ring_buffer_[static_cast<int>(wp % RING_BUFFER_SIZE)] = buffer[i];
        wp++;
    }
    ring_write_pos_.store(wp, std::memory_order_release);
    samples_written_ += num_samples;

    // Update waveform ring buffer (lock-free)
    for (int i = 0; i < num_samples; ++i) {
        float abs_val = std::fabs(buffer[i]);
        if (abs_val > bin_peak_) bin_peak_ = abs_val;
        bin_sample_count_++;
        if (bin_sample_count_ >= samples_per_bin_) {
            int pos = waveform_write_pos_.load() % WAVEFORM_SIZE;
            waveform_buf_[pos].store(bin_peak_);
            waveform_write_pos_.store(pos + 1);
            current_peak_.store(bin_peak_);
            bin_peak_ = 0.0f;
            bin_sample_count_ = 0;
        }
    }
}

void Recorder::writer_thread_func() {
    while (true) {
        int64_t rp = ring_read_pos_.load(std::memory_order_relaxed);
        int64_t wp = ring_write_pos_.load(std::memory_order_acquire);
        int64_t available = wp - rp;

        if (available > 0) {
            // Drain available samples: convert float -> int16 PCM and write to disk
            while (available > 0) {
                int chunk = static_cast<int>(std::min(available,
                            static_cast<int64_t>(pcm_buffer_.size())));
                for (int i = 0; i < chunk; ++i) {
                    float s = ring_buffer_[static_cast<int>((rp + i) % RING_BUFFER_SIZE)];
                    if (s > 1.0f) s = 1.0f;
                    if (s < -1.0f) s = -1.0f;
                    pcm_buffer_[i] = static_cast<int16_t>(s * 32767.0f);
                }
                file_.write(reinterpret_cast<const char*>(pcm_buffer_.data()),
                            chunk * sizeof(int16_t));
                rp += chunk;
                available -= chunk;
            }
            ring_read_pos_.store(rp, std::memory_order_release);
        } else {
            // No data available — exit if stopped, otherwise poll briefly
            if (!writer_running_.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

void Recorder::get_waveform(float* out, int count) const {
    int wp = waveform_write_pos_.load();
    for (int i = 0; i < count; ++i) {
        int idx = (wp - count + i + WAVEFORM_SIZE * 2) % WAVEFORM_SIZE;
        out[i] = waveform_buf_[idx].load();
    }
}

float Recorder::get_duration() const {
    int64_t total = samples_written_.load();
    if (sample_rate_ <= 0) return 0.0f;
    return static_cast<float>(total) / sample_rate_;
}

void Recorder::write_wav_header() {
    // Write a placeholder WAV header (44 bytes)
    // Will be finalized when recording stops
    char header[44];
    std::memset(header, 0, 44);

    int byte_rate = sample_rate_ * channels_ * 2; // 16-bit = 2 bytes
    int block_align = channels_ * 2;

    // RIFF header
    std::memcpy(header + 0, "RIFF", 4);
    // Chunk size (placeholder — filled in finalize)
    std::memcpy(header + 8, "WAVE", 4);

    // fmt sub-chunk
    std::memcpy(header + 12, "fmt ", 4);
    int fmt_size = 16;
    std::memcpy(header + 16, &fmt_size, 4);
    int16_t audio_format = 1; // PCM
    std::memcpy(header + 20, &audio_format, 2);
    int16_t num_channels = static_cast<int16_t>(channels_);
    std::memcpy(header + 22, &num_channels, 2);
    std::memcpy(header + 24, &sample_rate_, 4);
    std::memcpy(header + 28, &byte_rate, 4);
    int16_t block_align_16 = static_cast<int16_t>(block_align);
    std::memcpy(header + 32, &block_align_16, 2);
    int16_t bits_per_sample = 16;
    std::memcpy(header + 34, &bits_per_sample, 2);

    // data sub-chunk
    std::memcpy(header + 36, "data", 4);
    // Data size (placeholder — filled in finalize)

    file_.write(header, 44);
}

void Recorder::finalize_wav_header() {
    if (!file_.is_open()) return;

    int64_t total_samples = samples_written_.load();
    int data_size = static_cast<int>(total_samples * channels_ * 2);
    int riff_size = data_size + 36;

    // Seek back and write correct sizes
    file_.seekp(4, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(&riff_size), 4);

    file_.seekp(40, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(&data_size), 4);

    // Seek to end
    file_.seekp(0, std::ios::end);
}

void Recorder::write_metadata(const std::string& wav_path, AudioEngine& engine) {
    // Write a JSON sidecar file with recording details
    std::string meta_path = wav_path;
    // Replace .wav with .meta.json
    size_t dot = meta_path.rfind('.');
    if (dot != std::string::npos) {
        meta_path = meta_path.substr(0, dot);
    }
    meta_path += ".meta.json";

    std::ofstream meta(meta_path);
    if (!meta.is_open()) {
        std::cerr << "Could not write metadata: " << meta_path << std::endl;
        return;
    }

    std::time_t now = std::time(nullptr);
    char timebuf[64];
    std::tm time_info;
#ifdef _WIN32
    localtime_s(&time_info, &now);
#else
    localtime_r(&now, &time_info);
#endif
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", &time_info);

    float duration = get_duration();

    meta << "{\n";
    meta << "  \"recording\": {\n";
    meta << "    \"filename\": \"" << wav_path << "\",\n";
    meta << "    \"recorded_at\": \"" << timebuf << "\",\n";
    meta << "    \"duration_seconds\": " << duration << ",\n";
    meta << "    \"total_samples\": " << samples_written_.load() << ",\n";
    meta << "    \"format\": \"WAV PCM 16-bit\",\n";
    meta << "    \"sample_rate\": " << sample_rate_ << ",\n";
    meta << "    \"channels\": " << channels_ << ",\n";
    meta << "    \"bit_depth\": 16\n";
    meta << "  },\n";

    meta << "  \"audio_settings\": {\n";
    meta << "    \"input_device\": \"" << engine.get_input_device_name() << "\",\n";
    meta << "    \"output_device\": \"" << engine.get_output_device_name() << "\",\n";
    meta << "    \"engine_sample_rate\": " << engine.get_sample_rate() << ",\n";
    meta << "    \"buffer_size\": " << engine.get_buffer_size() << ",\n";
    meta << "    \"input_gain\": " << engine.get_input_gain() << ",\n";
    meta << "    \"output_gain\": " << engine.get_output_gain() << "\n";
    meta << "  },\n";

    meta << "  \"signal_chain\": [\n";
    auto& effects = engine.effects();
    for (size_t i = 0; i < effects.size(); ++i) {
        auto& fx = effects[i];
        meta << "    {\n";
        meta << "      \"name\": \"" << fx->name() << "\",\n";
        meta << "      \"enabled\": " << (fx->is_enabled() ? "true" : "false") << ",\n";
        meta << "      \"mix\": " << fx->get_mix() << ",\n";
        meta << "      \"parameters\": {\n";
        auto& params = fx->params();
        for (size_t p = 0; p < params.size(); ++p) {
            meta << "        \"" << params[p].name << "\": " << params[p].value;
            if (p + 1 < params.size()) meta << ",";
            meta << "\n";
        }
        meta << "      }\n";
        meta << "    }";
        if (i + 1 < effects.size()) meta << ",";
        meta << "\n";
    }
    meta << "  ]\n";
    meta << "}\n";

    meta.close();
    std::cout << "Metadata written: " << meta_path << std::endl;
}

} // namespace GuitarAmp
