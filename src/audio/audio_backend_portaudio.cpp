// =============================================================================
// PortAudio backend — native desktop (Linux, macOS, Windows)
//
// Implements AudioEngine member functions: initialize, shutdown, start, stop,
// restart, and device management. Also provides the audio callback and the
// AudioBackendState factory / destructor.
// =============================================================================

#include "audio/audio_engine.h"
#include "audio/audio_backend.h"
#include <portaudio.h>
#ifdef _WIN32
#include <pa_win_wasapi.h>
#endif
#include <cstring>
#include <iostream>
#include <cctype>
#include <algorithm>

namespace GuitarAmp {

// -----------------------------------------------------------------------------
// Backend state
// -----------------------------------------------------------------------------

struct AudioBackendState {
    PaStream* stream = nullptr;
};

AudioBackendState* create_audio_backend() {
    return new AudioBackendState();
}

void destroy_audio_backend(AudioBackendState* state) {
    delete state;
}

// -----------------------------------------------------------------------------
// File-local helpers (no class member access needed)
// -----------------------------------------------------------------------------

static bool is_usb_device_name(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    static const char* usb_keywords[] = {
        "usb", "guitar", "guitar link", "irig", "scarlett",
        "behringer", "focusrite", "presonus", "steinberg",
        "audio interface", "line 6", "rocksmith", "umc",
        "um2", "uphoria", "podcast", "xenyx"
    };

    for (const auto* keyword : usb_keywords) {
        if (lower.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static int get_host_api_priority(PaHostApiTypeId type) {
#if defined(__linux__)
    switch (type) {
        case paJACK:          return 100;
        case paALSA:          return 70;
        default:              return 10;
    }
#elif defined(_WIN32)
    switch (type) {
        case paASIO:          return 100;
        case paWASAPI:        return 90;
        case paDirectSound:   return 40;
        case paMME:           return 10;
        default:              return 20;
    }
#elif defined(__APPLE__)
    switch (type) {
        case paCoreAudio:     return 100;
        default:              return 30;
    }
#else
    (void)type;
    return 30;
#endif
}

static bool is_projector_or_hdmi(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lower.find("epson") != std::string::npos
        || lower.find("projector") != std::string::npos
        || lower.find("hdmi") != std::string::npos
        || lower.find("displayport") != std::string::npos;
}

static bool devices_share_host_api(int input_dev, int output_dev) {
    const PaDeviceInfo* in_info = Pa_GetDeviceInfo(input_dev);
    const PaDeviceInfo* out_info = Pa_GetDeviceInfo(output_dev);
    if (!in_info || !out_info) return false;
    return in_info->hostApi == out_info->hostApi;
}

/** @brief Auto-detect the best input/output device pair. */
static void auto_detect_devices(int& input_device, int& output_device, int& sample_rate) {
    int device_count = Pa_GetDeviceCount();
    int num_apis = Pa_GetHostApiCount();

    // Phase 1: Print all devices
    std::cout << "\nDetected audio devices:" << std::endl;
    for (int i = 0; i < device_count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) continue;
        bool is_usb = is_usb_device_name(info->name);
        const PaHostApiInfo* api = Pa_GetHostApiInfo(info->hostApi);
        const char* api_name = api ? api->name : "Unknown";
        if (info->maxInputChannels > 0) {
            std::cout << "  [IN]  " << info->name
                      << " (" << api_name << ")"
                      << (is_usb ? " [USB]" : "") << std::endl;
        }
        if (info->maxOutputChannels > 0) {
            std::cout << "  [OUT] " << info->name
                      << " (" << api_name << ")"
                      << (is_usb ? " [USB]" : "") << std::endl;
        }
    }

    // Phase 2: For each host API (ranked by priority), find the best
    // USB input + non-USB/non-projector output PAIR on the SAME API.
    struct ApiCandidate {
        int api_index;
        int priority;
        int usb_input;
        int best_output;
        std::string api_name;
    };

    std::vector<ApiCandidate> candidates;
    for (int a = 0; a < num_apis; ++a) {
        const PaHostApiInfo* api = Pa_GetHostApiInfo(a);
        if (!api) continue;

        ApiCandidate c;
        c.api_index = a;
        c.priority = get_host_api_priority(api->type);
        c.usb_input = -1;
        c.best_output = -1;
        c.api_name = api->name;

        for (int d = 0; d < api->deviceCount; ++d) {
            int dev_idx = Pa_HostApiDeviceIndexToDeviceIndex(a, d);
            const PaDeviceInfo* info = Pa_GetDeviceInfo(dev_idx);
            if (!info) continue;

            bool is_usb = is_usb_device_name(info->name);

            if (is_usb && info->maxInputChannels > 0 && c.usb_input < 0) {
                c.usb_input = dev_idx;
            }
            if (!is_usb && !is_projector_or_hdmi(info->name)
                && info->maxOutputChannels > 0 && c.best_output < 0) {
                c.best_output = dev_idx;
            }
        }

        if (c.best_output < 0) {
            for (int d = 0; d < api->deviceCount; ++d) {
                int dev_idx = Pa_HostApiDeviceIndexToDeviceIndex(a, d);
                const PaDeviceInfo* info = Pa_GetDeviceInfo(dev_idx);
                if (!info) continue;
                if (!is_usb_device_name(info->name) && info->maxOutputChannels > 0) {
                    c.best_output = dev_idx;
                    break;
                }
            }
        }

        candidates.push_back(c);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const ApiCandidate& a, const ApiCandidate& b) {
                  return a.priority > b.priority;
              });

    // Phase 3: Pick the best API that has BOTH a USB input AND an output
    bool found_pair = false;
    for (auto& c : candidates) {
        if (c.usb_input >= 0 && c.best_output >= 0) {
            input_device = c.usb_input;
            output_device = c.best_output;

            const PaDeviceInfo* in_info = Pa_GetDeviceInfo(input_device);
            const PaDeviceInfo* out_info = Pa_GetDeviceInfo(output_device);

            std::cout << "\n>> Auto-selected (same API: " << c.api_name << "):" << std::endl;
            std::cout << "   INPUT:  " << in_info->name << " [USB Guitar Cable]" << std::endl;
            std::cout << "   OUTPUT: " << out_info->name << " [Speakers]" << std::endl;

            if (in_info->defaultSampleRate > 0) {
                sample_rate = static_cast<int>(in_info->defaultSampleRate);
                std::cout << "   Rate:   " << sample_rate << " Hz (device native)" << std::endl;
            }

            found_pair = true;
            break;
        }
    }

    // Fallback: no USB input found
    if (!found_pair) {
        for (auto& c : candidates) {
            if (c.best_output >= 0) {
                const PaHostApiInfo* api = Pa_GetHostApiInfo(c.api_index);
                for (int d = 0; d < api->deviceCount; ++d) {
                    int dev_idx = Pa_HostApiDeviceIndexToDeviceIndex(c.api_index, d);
                    const PaDeviceInfo* info = Pa_GetDeviceInfo(dev_idx);
                    if (info && info->maxInputChannels > 0) {
                        input_device = dev_idx;
                        output_device = c.best_output;

                        std::cout << "\n>> No USB guitar cable detected." << std::endl;
                        std::cout << "   Using " << c.api_name << " defaults:" << std::endl;
                        std::cout << "   INPUT:  " << info->name << std::endl;
                        std::cout << "   OUTPUT: " << Pa_GetDeviceInfo(output_device)->name << std::endl;

                        found_pair = true;
                        break;
                    }
                }
                if (found_pair) break;
            }
        }
    }

    // Last resort: system defaults
    if (!found_pair) {
        input_device = Pa_GetDefaultInputDevice();
        output_device = Pa_GetDefaultOutputDevice();
        std::cout << "\n>> Using system default input/output devices." << std::endl;
    }
}

// -----------------------------------------------------------------------------
// PortAudio audio callback (file-local, calls engine->process_audio)
// -----------------------------------------------------------------------------

static int pa_audio_callback(const void* input, void* output,
                              unsigned long frame_count,
                              const PaStreamCallbackTimeInfo* /*time_info*/,
                              PaStreamCallbackFlags /*status_flags*/,
                              void* user_data) {
    auto* engine = static_cast<AudioEngine*>(user_data);
    const auto* in = static_cast<const float*>(input);
    auto* out = static_cast<float*>(output);

    if (!in || !out) {
        if (out) std::memset(out, 0, frame_count * 2 * sizeof(float));
        return paContinue;
    }

    engine->process_audio(in, out, static_cast<int>(frame_count));
    return paContinue;
}

// =============================================================================
// AudioEngine member functions — PortAudio implementations
// =============================================================================

bool AudioEngine::initialize() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio init failed: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    initialized_ = true;

    auto_detect_devices(input_device_, output_device_, sample_rate_);

    return true;
}

void AudioEngine::shutdown() {
    stop();
    if (initialized_) {
        Pa_Terminate();
        initialized_ = false;
    }
}

bool AudioEngine::start() {
    if (!initialized_ || running_) return false;

    const PaDeviceInfo* in_dev = Pa_GetDeviceInfo(input_device_);
    const PaDeviceInfo* out_dev = Pa_GetDeviceInfo(output_device_);
    (void)in_dev; (void)out_dev;

    double desired_latency = static_cast<double>(buffer_size_) / sample_rate_;

    PaStreamParameters input_params;
    input_params.device = input_device_;
    input_params.channelCount = 1;
    input_params.sampleFormat = paFloat32;
    input_params.suggestedLatency = desired_latency;
    input_params.hostApiSpecificStreamInfo = nullptr;

    PaStreamParameters output_params;
    output_params.device = output_device_;
    output_params.channelCount = 2;
    output_params.sampleFormat = paFloat32;
    output_params.suggestedLatency = desired_latency;
    output_params.hostApiSpecificStreamInfo = nullptr;

#ifdef _WIN32
    PaWasapiStreamInfo wasapi_in_info = {};
    PaWasapiStreamInfo wasapi_out_info = {};
    if (in_dev) {
        const PaHostApiInfo* api = Pa_GetHostApiInfo(in_dev->hostApi);
        if (api && api->type == paWASAPI) {
            wasapi_in_info.size = sizeof(PaWasapiStreamInfo);
            wasapi_in_info.hostApiType = paWASAPI;
            wasapi_in_info.version = 1;
            wasapi_in_info.flags = paWinWasapiExclusive;
            input_params.hostApiSpecificStreamInfo = &wasapi_in_info;

            wasapi_out_info.size = sizeof(PaWasapiStreamInfo);
            wasapi_out_info.hostApiType = paWASAPI;
            wasapi_out_info.version = 1;
            wasapi_out_info.flags = paWinWasapiExclusive;
            output_params.hostApiSpecificStreamInfo = &wasapi_out_info;

            std::cout << "  Using WASAPI Exclusive Mode" << std::endl;
        }
    }
#endif

    unsigned long frames = static_cast<unsigned long>(buffer_size_);

    PaError err = Pa_OpenStream(
        &backend_->stream,
        &input_params,
        &output_params,
        sample_rate_,
        frames,
        paClipOff | paDitherOff,
        pa_audio_callback,
        this
    );

    if (err != paNoError) {
        std::cerr << "Failed to open stream: " << Pa_GetErrorText(err) << std::endl;
        std::cerr << "Retrying with buffer size " << buffer_size_ << "..." << std::endl;
        err = Pa_OpenStream(
            &backend_->stream,
            &input_params,
            &output_params,
            sample_rate_,
            buffer_size_,
            paClipOff | paDitherOff,
            pa_audio_callback,
            this
        );
        if (err != paNoError) {
            std::cerr << "Failed to open stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
    }

    err = Pa_StartStream(backend_->stream);
    if (err != paNoError) {
        std::cerr << "Failed to start stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(backend_->stream);
        backend_->stream = nullptr;
        return false;
    }

    running_ = true;
    const PaStreamInfo* si = Pa_GetStreamInfo(backend_->stream);
    const PaDeviceInfo* in_info = Pa_GetDeviceInfo(input_device_);
    const PaDeviceInfo* out_info = Pa_GetDeviceInfo(output_device_);
    std::cout << "Audio stream started:" << std::endl;
    std::cout << "  Input:   " << (in_info ? in_info->name : "Unknown") << std::endl;
    std::cout << "  Output:  " << (out_info ? out_info->name : "Unknown") << std::endl;
    std::cout << "  Rate:    " << (si ? si->sampleRate : sample_rate_) << " Hz" << std::endl;
    if (si) {
        std::cout << "  Latency: in=" << (si->inputLatency * 1000.0) << " ms"
                  << "  out=" << (si->outputLatency * 1000.0) << " ms" << std::endl;
    }
    return true;
}

void AudioEngine::stop() {
    if (backend_->stream) {
        if (running_) {
            Pa_StopStream(backend_->stream);
            running_ = false;
        }
        Pa_CloseStream(backend_->stream);
        backend_->stream = nullptr;
    }
}

bool AudioEngine::restart() {
    stop();
    bool ok = start();
    if (!ok) {
        last_error_ = "Failed to restart audio stream. Check device settings.";
        std::cerr << "[Amplitron] " << last_error_ << std::endl;
    } else {
        last_error_.clear();
    }
    return ok;
}

// =============================================================================
// Device management — PortAudio
// =============================================================================

std::string AudioEngine::get_input_device_name() const {
    if (input_device_ >= 0) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(input_device_);
        if (info) return info->name;
    }
    return "None";
}

std::string AudioEngine::get_output_device_name() const {
    if (output_device_ >= 0) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(output_device_);
        if (info) return info->name;
    }
    return "None";
}

std::vector<AudioDeviceInfo> AudioEngine::get_input_devices() const {
    std::vector<AudioDeviceInfo> devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            devices.push_back({
                i, info->name,
                info->maxInputChannels, info->maxOutputChannels,
                info->defaultSampleRate,
                is_usb_device_name(info->name)
            });
        }
    }
    return devices;
}

std::vector<AudioDeviceInfo> AudioEngine::get_output_devices() const {
    std::vector<AudioDeviceInfo> devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            devices.push_back({
                i, info->name,
                info->maxInputChannels, info->maxOutputChannels,
                info->defaultSampleRate,
                is_usb_device_name(info->name)
            });
        }
    }
    return devices;
}

bool AudioEngine::set_input_device(int device_index) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(device_index);
    if (!info || info->maxInputChannels < 1) {
        last_error_ = "Invalid input device.";
        return false;
    }

    if (!devices_share_host_api(device_index, output_device_)) {
        const PaDeviceInfo* out_info = Pa_GetDeviceInfo(output_device_);
        const PaHostApiInfo* in_api = Pa_GetHostApiInfo(info->hostApi);
        const PaHostApiInfo* out_api = out_info ? Pa_GetHostApiInfo(out_info->hostApi) : nullptr;
        std::cerr << "[Amplitron] Warning: Input (" << (in_api ? in_api->name : "?")
                  << ") and output (" << (out_api ? out_api->name : "?")
                  << ") are on different host APIs. Stream may fail." << std::endl;
    }

    int prev_device = input_device_;
    bool was_running = running_;
    if (was_running) stop();
    input_device_ = device_index;
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed to start with new input device. Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            input_device_ = prev_device;
            start();
            return false;
        }
        last_error_.clear();
    }
    return true;
}

bool AudioEngine::set_output_device(int device_index) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(device_index);
    if (!info || info->maxOutputChannels < 1) {
        last_error_ = "Invalid output device.";
        return false;
    }

    if (!devices_share_host_api(input_device_, device_index)) {
        const PaDeviceInfo* in_info = Pa_GetDeviceInfo(input_device_);
        const PaHostApiInfo* in_api = in_info ? Pa_GetHostApiInfo(in_info->hostApi) : nullptr;
        const PaHostApiInfo* out_api = Pa_GetHostApiInfo(info->hostApi);
        std::cerr << "[Amplitron] Warning: Input (" << (in_api ? in_api->name : "?")
                  << ") and output (" << (out_api ? out_api->name : "?")
                  << ") are on different host APIs. Stream may fail." << std::endl;
    }

    int prev_device = output_device_;
    bool was_running = running_;
    if (was_running) stop();
    output_device_ = device_index;
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed to start with new output device. Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            output_device_ = prev_device;
            start();
            return false;
        }
        last_error_.clear();
    }
    return true;
}

} // namespace GuitarAmp
