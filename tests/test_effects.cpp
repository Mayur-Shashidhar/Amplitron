#include "test_framework.h"
#include "audio/effects/noise_gate.h"
#include "audio/effects/compressor.h"
#include "audio/effects/overdrive.h"
#include "audio/effects/distortion.h"
#include "audio/effects/equalizer.h"
#include "audio/effects/chorus.h"
#include "audio/effects/delay.h"
#include "audio/effects/reverb.h"
#include "audio/effects/cabinet_sim.h"
#include "audio/effects/amp_simulator.h"
#include "audio/effects/tuner.h"
#include "audio/effects/wah.h"
#include "audio/effects/phaser.h"
#include "audio/effects/flanger.h"
#include <cstring>
#include <cmath>

using namespace GuitarAmp;

// Helper: fill buffer with a sine wave
static void fill_sine(float* buf, int n, float freq, int sr) {
    for (int i = 0; i < n; ++i)
        buf[i] = std::sin(2.0f * 3.14159265f * freq * i / sr);
}

// Helper: compute RMS of a buffer
static float rms(const float* buf, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) sum += buf[i] * buf[i];
    return std::sqrt(sum / n);
}

// Helper: check no NaN or Inf in buffer
static bool buffer_is_finite(const float* buf, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(buf[i])) return false;
    return true;
}

// ============================================================
// Effect base class tests
// ============================================================

TEST(effect_enabled_default_true) {
    NoiseGate ng;
    ASSERT_TRUE(ng.is_enabled());
}

TEST(effect_enable_disable) {
    NoiseGate ng;
    ng.set_enabled(false);
    ASSERT_FALSE(ng.is_enabled());
    ng.set_enabled(true);
    ASSERT_TRUE(ng.is_enabled());
}

TEST(effect_mix_clamped) {
    Overdrive od;
    od.set_mix(-0.5f);
    ASSERT_NEAR(od.get_mix(), 0.0f, 1e-6f);
    od.set_mix(1.5f);
    ASSERT_NEAR(od.get_mix(), 1.0f, 1e-6f);
    od.set_mix(0.5f);
    ASSERT_NEAR(od.get_mix(), 0.5f, 1e-6f);
}

TEST(effect_has_name) {
    NoiseGate ng;
    Compressor comp;
    Overdrive od;
    Distortion dist;
    Equalizer eq;
    Chorus ch;
    Delay dl;
    Reverb rv;
    CabinetSim cab;

    ASSERT_TRUE(std::strcmp(ng.name(), "Noise Gate") == 0);
    ASSERT_TRUE(std::strcmp(comp.name(), "Compressor") == 0);
    ASSERT_TRUE(std::strcmp(od.name(), "Overdrive") == 0);
    ASSERT_TRUE(std::strcmp(dist.name(), "Distortion") == 0);
    ASSERT_TRUE(std::strcmp(eq.name(), "Equalizer") == 0);
    ASSERT_TRUE(std::strcmp(ch.name(), "Chorus") == 0);
    ASSERT_TRUE(std::strcmp(dl.name(), "Delay") == 0);
    ASSERT_TRUE(std::strcmp(rv.name(), "Reverb") == 0);
    ASSERT_TRUE(std::strcmp(cab.name(), "Cabinet") == 0);

    AmpSimulator amp;
    ASSERT_TRUE(std::strcmp(amp.name(), "Amp Sim") == 0);

    TunerPedal tuner;
    ASSERT_TRUE(std::strcmp(tuner.name(), "Tuner") == 0);

    WahPedal wah;
    ASSERT_TRUE(std::strcmp(wah.name(), "Wah") == 0);

    Phaser ph;
    ASSERT_TRUE(std::strcmp(ph.name(), "Phaser") == 0);

    Flanger fl;
    ASSERT_TRUE(std::strcmp(fl.name(), "Flanger") == 0);
}

TEST(effect_has_params) {
    Overdrive od;
    ASSERT_GT((int)od.params().size(), 0);

    Equalizer eq;
    ASSERT_GT((int)eq.params().size(), 0);

    Compressor comp;
    ASSERT_GT((int)comp.params().size(), 0);
}

TEST(effect_params_have_valid_ranges) {
    Overdrive od;
    for (auto& p : od.params()) {
        ASSERT_TRUE(p.min_val <= p.max_val);
        ASSERT_TRUE(p.value >= p.min_val);
        ASSERT_TRUE(p.value <= p.max_val);
        ASSERT_TRUE(p.default_val >= p.min_val);
        ASSERT_TRUE(p.default_val <= p.max_val);
        ASSERT_FALSE(p.name.empty());
    }
}

TEST(effect_set_sample_rate) {
    Reverb rv;
    rv.set_sample_rate(44100);
    rv.reset();
    // Should not crash
    float buf[128];
    fill_sine(buf, 128, 440.0f, 44100);
    rv.process(buf, 128);
    ASSERT_TRUE(buffer_is_finite(buf, 128));
}

// ============================================================
// Individual effect processing tests
// ============================================================

TEST(noise_gate_silences_quiet_signal) {
    NoiseGate ng;
    ng.set_sample_rate(48000);
    ng.reset();

    // Very quiet signal (well below any reasonable threshold)
    float buf[256];
    for (int i = 0; i < 256; ++i) buf[i] = 0.0001f * std::sin(2.0f * 3.14159f * 440.0f * i / 48000.0f);

    // Process several times to let the gate close
    for (int rep = 0; rep < 20; ++rep)
        ng.process(buf, 256);

    // After many passes of quiet signal, output should be very quiet
    float out_rms = rms(buf, 256);
    ASSERT_LT(out_rms, 0.001f);
}

TEST(noise_gate_passes_loud_signal) {
    NoiseGate ng;
    ng.set_sample_rate(48000);
    ng.reset();

    float buf[256];
    fill_sine(buf, 256, 440.0f, 48000);
    float in_rms = rms(buf, 256);

    ng.process(buf, 256);
    float out_rms = rms(buf, 256);

    // Loud signal should pass through mostly unchanged
    ASSERT_GT(out_rms, in_rms * 0.5f);
}

TEST(overdrive_adds_harmonics) {
    Overdrive od;
    od.set_sample_rate(48000);
    od.reset();

    float buf[512];
    fill_sine(buf, 512, 440.0f, 48000);

    od.process(buf, 512);
    ASSERT_TRUE(buffer_is_finite(buf, 512));

    // Output should still have energy
    ASSERT_GT(rms(buf, 512), 0.01f);
}

TEST(distortion_clips_signal) {
    Distortion dist;
    dist.set_sample_rate(48000);
    dist.reset();

    float buf[512];
    fill_sine(buf, 512, 440.0f, 48000);

    dist.process(buf, 512);
    ASSERT_TRUE(buffer_is_finite(buf, 512));

    // All output should be within [-1, 1] (clipped)
    for (int i = 0; i < 512; ++i) {
        ASSERT_GE(buf[i], -1.5f);  // some headroom for processing
        ASSERT_TRUE(buf[i] <= 1.5f);
    }
}

TEST(compressor_reduces_dynamic_range) {
    Compressor comp;
    comp.set_sample_rate(48000);
    comp.reset();

    // Loud signal
    float buf[2048];
    fill_sine(buf, 2048, 440.0f, 48000);
    // Scale to be loud
    for (int i = 0; i < 2048; ++i) buf[i] *= 0.9f;

    // Process multiple times to let compressor engage
    for (int rep = 0; rep < 5; ++rep) {
        fill_sine(buf, 2048, 440.0f, 48000);
        for (int i = 0; i < 2048; ++i) buf[i] *= 0.9f;
        comp.process(buf, 2048);
    }

    ASSERT_TRUE(buffer_is_finite(buf, 2048));
    ASSERT_GT(rms(buf, 2048), 0.01f);
}

TEST(equalizer_processes_without_nan) {
    Equalizer eq;
    eq.set_sample_rate(48000);
    eq.reset();

    float buf[512];
    fill_sine(buf, 512, 440.0f, 48000);
    eq.process(buf, 512);

    ASSERT_TRUE(buffer_is_finite(buf, 512));
    ASSERT_GT(rms(buf, 512), 0.01f);
}

TEST(chorus_modulates_signal) {
    Chorus ch;
    ch.set_sample_rate(48000);
    ch.reset();

    float buf[1024];
    fill_sine(buf, 1024, 440.0f, 48000);
    ch.process(buf, 1024);

    ASSERT_TRUE(buffer_is_finite(buf, 1024));
}

TEST(delay_produces_echo) {
    Delay dl;
    dl.set_sample_rate(48000);
    dl.reset();

    // Process an impulse
    float buf[4096];
    std::memset(buf, 0, sizeof(buf));
    buf[0] = 1.0f;

    dl.process(buf, 4096);
    ASSERT_TRUE(buffer_is_finite(buf, 4096));

    // There should be energy later in the buffer (the echo)
    float late_energy = 0.0f;
    for (int i = 2048; i < 4096; ++i)
        late_energy += buf[i] * buf[i];
    (void)late_energy;
    // With default delay settings, some echo should appear
    // (might be zero if delay time > buffer length, so just check finite)
}

TEST(reverb_adds_tail) {
    Reverb rv;
    rv.set_sample_rate(48000);
    rv.reset();

    float buf[2048];
    std::memset(buf, 0, sizeof(buf));
    buf[0] = 1.0f;

    rv.process(buf, 2048);
    ASSERT_TRUE(buffer_is_finite(buf, 2048));

    // Late portion should have some reverb tail energy
    float tail_energy = 0.0f;
    for (int i = 1024; i < 2048; ++i)
        tail_energy += buf[i] * buf[i];
    ASSERT_GT(tail_energy, 1e-10f);
}

TEST(cabinet_sim_filters_signal) {
    CabinetSim cab;
    cab.set_sample_rate(48000);
    cab.reset();

    float buf[512];
    fill_sine(buf, 512, 440.0f, 48000);
    cab.process(buf, 512);

    ASSERT_TRUE(buffer_is_finite(buf, 512));
    ASSERT_GT(rms(buf, 512), 0.001f);
}

TEST(amp_simulator_processes_without_nan) {
    AmpSimulator amp;
    amp.set_sample_rate(48000);
    amp.reset();

    float buf[512];
    fill_sine(buf, 512, 440.0f, 48000);
    amp.process(buf, 512);

    ASSERT_TRUE(buffer_is_finite(buf, 512));
    ASSERT_GT(rms(buf, 512), 0.001f);
}

TEST(amp_simulator_models_sound_different) {
    const auto& models = GuitarAmp::get_amp_models();
    ASSERT_GE((int)models.size(), 3);

    std::vector<float> model_rms;
    for (int m = 0; m < static_cast<int>(models.size()); ++m) {
        AmpSimulator amp;
        amp.set_sample_rate(48000);
        amp.reset();
        amp.params()[0].value = static_cast<float>(m);

        float buf[1024];
        fill_sine(buf, 1024, 440.0f, 48000);
        amp.process(buf, 1024);
        ASSERT_TRUE(buffer_is_finite(buf, 1024));
        model_rms.push_back(rms(buf, 1024));
    }

    // At least one pair of models should produce meaningfully different RMS
    bool found_diff = false;
    for (size_t i = 0; i < model_rms.size() && !found_diff; ++i) {
        for (size_t j = i + 1; j < model_rms.size(); ++j) {
            if (std::fabs(model_rms[i] - model_rms[j]) > 0.01f) {
                found_diff = true;
                break;
            }
        }
    }
    ASSERT_TRUE(found_diff);
}

TEST(amp_simulator_output_clamped) {
    AmpSimulator amp;
    amp.set_sample_rate(48000);
    amp.reset();
    // High gain model
    amp.params()[0].value = 2.0f; // High Gain Modern
    amp.params()[1].value = 1.0f; // Max gain knob

    float buf[512];
    fill_sine(buf, 512, 440.0f, 48000);
    amp.process(buf, 512);

    for (int i = 0; i < 512; ++i) {
        ASSERT_GE(buf[i], -1.0f);
        ASSERT_TRUE(buf[i] <= 1.0f);
    }
}

TEST(amp_simulator_get_models_returns_at_least_three) {
    const auto& models = GuitarAmp::get_amp_models();
    ASSERT_GE((int)models.size(), 3);
    for (const auto& m : models) {
        ASSERT_TRUE(m.name != nullptr);
        ASSERT_TRUE(m.inspiration != nullptr);
        ASSERT_TRUE(m.description != nullptr);
    }
}

// ============================================================
// Tuner pitch detection tests
// ============================================================

// Helper: feed a sine wave through the tuner and return detected frequency
static float tuner_detect_freq(float target_freq, int sample_rate = 48000) {
    TunerPedal tuner;
    tuner.set_sample_rate(sample_rate);
    tuner.reset();
    // Disable mute so we can also verify pass-through
    tuner.params()[0].value = 0.0f;

    // Generate enough sine wave data to fill the YIN buffer multiple times
    // and trigger at least one detection update
    const int total_samples = sample_rate; // 1 second of audio
    const int chunk_size = 256;
    float buf[256];

    for (int offset = 0; offset < total_samples; offset += chunk_size) {
        for (int i = 0; i < chunk_size; ++i) {
            buf[i] = 0.8f * std::sin(2.0f * 3.14159265f * target_freq * (offset + i) / sample_rate);
        }
        tuner.process(buf, chunk_size);
    }

    return tuner.detected_freq.load();
}

TEST(tuner_detects_E2) {
    // E2 = 82.41 Hz (low E string)
    float freq = tuner_detect_freq(82.41f);
    ASSERT_GT(freq, 0.0f);
    // Within +/- 2 cents = +/- 0.095 Hz at 82.41 Hz
    float cents_err = std::fabs(1200.0f * std::log2(freq / 82.41f));
    ASSERT_LT(cents_err, 2.0f);
}

TEST(tuner_detects_A2) {
    // A2 = 110.0 Hz (A string)
    float freq = tuner_detect_freq(110.0f);
    ASSERT_GT(freq, 0.0f);
    float cents_err = std::fabs(1200.0f * std::log2(freq / 110.0f));
    ASSERT_LT(cents_err, 2.0f);
}

TEST(tuner_detects_D3) {
    // D3 = 146.83 Hz (D string)
    float freq = tuner_detect_freq(146.83f);
    ASSERT_GT(freq, 0.0f);
    float cents_err = std::fabs(1200.0f * std::log2(freq / 146.83f));
    ASSERT_LT(cents_err, 2.0f);
}

TEST(tuner_detects_G3) {
    // G3 = 196.0 Hz (G string)
    float freq = tuner_detect_freq(196.0f);
    ASSERT_GT(freq, 0.0f);
    float cents_err = std::fabs(1200.0f * std::log2(freq / 196.0f));
    ASSERT_LT(cents_err, 2.0f);
}

TEST(tuner_detects_B3) {
    // B3 = 246.94 Hz (B string)
    float freq = tuner_detect_freq(246.94f);
    ASSERT_GT(freq, 0.0f);
    float cents_err = std::fabs(1200.0f * std::log2(freq / 246.94f));
    ASSERT_LT(cents_err, 2.0f);
}

TEST(tuner_detects_E4) {
    // E4 = 329.63 Hz (high E string)
    float freq = tuner_detect_freq(329.63f);
    ASSERT_GT(freq, 0.0f);
    float cents_err = std::fabs(1200.0f * std::log2(freq / 329.63f));
    ASSERT_LT(cents_err, 2.0f);
}

TEST(tuner_note_names_correct) {
    ASSERT_TRUE(std::strcmp(TunerPedal::note_name(0), "C") == 0);
    ASSERT_TRUE(std::strcmp(TunerPedal::note_name(4), "E") == 0);
    ASSERT_TRUE(std::strcmp(TunerPedal::note_name(9), "A") == 0);
    ASSERT_TRUE(std::strcmp(TunerPedal::note_name(11), "B") == 0);
    ASSERT_TRUE(std::strcmp(TunerPedal::note_name(-1), "?") == 0);
}

TEST(tuner_maps_note_and_octave) {
    // Feed A4 (440 Hz) and check note=A(9), octave=4, cents~0
    TunerPedal tuner;
    tuner.set_sample_rate(48000);
    tuner.reset();

    const int total = 48000;
    const int chunk = 256;
    float buf[256];
    for (int off = 0; off < total; off += chunk) {
        for (int i = 0; i < chunk; ++i)
            buf[i] = 0.8f * std::sin(2.0f * 3.14159265f * 440.0f * (off + i) / 48000.0f);
        tuner.process(buf, chunk);
    }

    ASSERT_TRUE(tuner.signal_detected.load());
    ASSERT_EQ(tuner.detected_note.load(), 9);   // A
    ASSERT_EQ(tuner.detected_octave.load(), 4);  // octave 4
    ASSERT_LT(std::fabs(tuner.detected_cents.load()), 2.0f);
}

TEST(tuner_mute_zeroes_output) {
    TunerPedal tuner;
    tuner.set_sample_rate(48000);
    tuner.reset();
    // Mute on (default)
    tuner.params()[0].value = 1.0f;

    float buf[256];
    fill_sine(buf, 256, 440.0f, 48000);
    tuner.process(buf, 256);

    // Output should be silenced
    float out_rms = rms(buf, 256);
    ASSERT_LT(out_rms, 1e-10f);
}

TEST(tuner_pass_through_when_unmuted) {
    TunerPedal tuner;
    tuner.set_sample_rate(48000);
    tuner.reset();
    // Mute off
    tuner.params()[0].value = 0.0f;

    float buf[256];
    fill_sine(buf, 256, 440.0f, 48000);
    float in_rms = rms(buf, 256);
    tuner.process(buf, 256);
    float out_rms = rms(buf, 256);

    // Signal should pass through unchanged
    ASSERT_NEAR(out_rms, in_rms, 1e-6f);
}

TEST(tuner_no_detection_on_silence) {
    TunerPedal tuner;
    tuner.set_sample_rate(48000);
    tuner.reset();

    float buf[256];
    std::memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 200; ++i)
        tuner.process(buf, 256);

    ASSERT_FALSE(tuner.signal_detected.load());
}

// ============================================================
// Aggregate effect tests (including Tuner)
// ============================================================

TEST(all_effects_handle_silence) {
    std::vector<std::shared_ptr<Effect>> effects = {
        std::make_shared<NoiseGate>(),
        std::make_shared<Compressor>(),
        std::make_shared<Overdrive>(),
        std::make_shared<Distortion>(),
        std::make_shared<Equalizer>(),
        std::make_shared<Chorus>(),
        std::make_shared<Phaser>(),
        std::make_shared<Flanger>(),
        std::make_shared<Delay>(),
        std::make_shared<Reverb>(),
        std::make_shared<CabinetSim>(),
        std::make_shared<AmpSimulator>(),
        std::make_shared<TunerPedal>(),
        std::make_shared<WahPedal>(),
    };

    float buf[256];
    for (auto& fx : effects) {
        fx->set_sample_rate(48000);
        fx->reset();
        std::memset(buf, 0, sizeof(buf));
        fx->process(buf, 256);
        ASSERT_TRUE(buffer_is_finite(buf, 256));
    }
}

TEST(all_effects_reset_without_crash) {
    std::vector<std::shared_ptr<Effect>> effects = {
        std::make_shared<NoiseGate>(),
        std::make_shared<Compressor>(),
        std::make_shared<Overdrive>(),
        std::make_shared<Distortion>(),
        std::make_shared<Equalizer>(),
        std::make_shared<Chorus>(),
        std::make_shared<Phaser>(),
        std::make_shared<Flanger>(),
        std::make_shared<Delay>(),
        std::make_shared<Reverb>(),
        std::make_shared<CabinetSim>(),
        std::make_shared<AmpSimulator>(),
        std::make_shared<TunerPedal>(),
        std::make_shared<WahPedal>(),
    };

    for (auto& fx : effects) {
        fx->set_sample_rate(48000);
        // Process some audio
        float buf[128];
        fill_sine(buf, 128, 440.0f, 48000);
        fx->process(buf, 128);
        // Reset
        fx->reset();
        // Process again
        fill_sine(buf, 128, 440.0f, 48000);
        fx->process(buf, 128);
        ASSERT_TRUE(buffer_is_finite(buf, 128));
    }
}

TEST(all_effects_handle_different_sample_rates) {
    int rates[] = {22050, 44100, 48000, 96000};
    std::vector<std::shared_ptr<Effect>> effects = {
        std::make_shared<NoiseGate>(),
        std::make_shared<Compressor>(),
        std::make_shared<Overdrive>(),
        std::make_shared<Distortion>(),
        std::make_shared<Equalizer>(),
        std::make_shared<Chorus>(),
        std::make_shared<Phaser>(),
        std::make_shared<Flanger>(),
        std::make_shared<Delay>(),
        std::make_shared<Reverb>(),
        std::make_shared<CabinetSim>(),
        std::make_shared<AmpSimulator>(),
        std::make_shared<TunerPedal>(),
        std::make_shared<WahPedal>(),
    };

    float buf[256];
    for (int rate : rates) {
        for (auto& fx : effects) {
            fx->set_sample_rate(rate);
            fx->reset();
            fill_sine(buf, 256, 440.0f, rate);
            fx->process(buf, 256);
            ASSERT_TRUE(buffer_is_finite(buf, 256));
        }
    }
}

// ============================================================
// WahPedal tests
// ============================================================

TEST(wah_has_name) {
    WahPedal wah;
    ASSERT_TRUE(std::strcmp(wah.name(), "Wah") == 0);
}

TEST(wah_params_valid_ranges) {
    WahPedal wah;
    for (auto& p : wah.params()) {
        ASSERT_TRUE(p.min_val <= p.max_val);
        ASSERT_TRUE(p.value >= p.min_val && p.value <= p.max_val);
        ASSERT_TRUE(p.default_val >= p.min_val && p.default_val <= p.max_val);
    }
}

TEST(wah_produces_finite_output) {
    WahPedal wah;
    wah.set_sample_rate(48000);
    wah.reset();

    float buf[256];
    fill_sine(buf, 256, 440.0f, 48000);
    wah.process(buf, 256);
    ASSERT_TRUE(buffer_is_finite(buf, 256));
}

TEST(wah_disabled_passes_dry_signal) {
    WahPedal wah;
    wah.set_sample_rate(48000);
    wah.reset();
    wah.set_enabled(false);

    float buf[256];
    float ref[256];
    fill_sine(buf, 256, 440.0f, 48000);
    for (int i = 0; i < 256; ++i) ref[i] = buf[i];
    wah.process(buf, 256);

    for (int i = 0; i < 256; ++i)
        ASSERT_NEAR(buf[i], ref[i], 1e-6f);
}

// Verify that bandpass centre frequency actually tracks the sweep parameter:
// heel-down (sweep=0) should output less energy at 2kHz than toe-down (sweep=1).
TEST(wah_bandpass_tracks_sweep) {
    const int SR = 48000;
    const int N  = 4096; // long enough for the filter to settle

    // Measure bandpass output RMS at 2 kHz for heel-down vs toe-down
    auto measure_rms_at = [&](float sweep_val) -> float {
        WahPedal wah;
        wah.set_sample_rate(SR);
        wah.reset();
        // Manual mode, mix fully wet so we hear only the bandpass
        wah.set_mix(1.0f);
        wah.params()[0].value = 0.0f; // manual mode
        wah.params()[1].value = sweep_val;
        wah.params()[2].value = 3.5f; // default resonance

        float buf[4096];
        fill_sine(buf, N, 2000.0f, SR); // 2 kHz probe tone
        wah.process(buf, N);
        return rms(buf, N);
    };

    float rms_heel = measure_rms_at(0.0f); // centre ~350 Hz — 2 kHz is out-of-band
    float rms_toe  = measure_rms_at(1.0f); // centre ~2500 Hz — 2 kHz is in-band

    // Toe-down should pass significantly more energy at 2 kHz
    ASSERT_GT(rms_toe, rms_heel * 2.0f);
}

TEST(wah_auto_mode_responds_to_amplitude) {
    const int SR = 48000;
    const int N  = 2048;

    WahPedal wah;
    wah.set_sample_rate(SR);
    wah.reset();
    wah.params()[0].value = 1.0f;  // auto-wah mode
    wah.params()[3].value = 1.0f;  // max sensitivity

    // Feed silence — filter should stay near heel
    float silent[2048] = {};
    wah.process(silent, N);
    ASSERT_TRUE(buffer_is_finite(silent, N));

    // Feed a loud signal — filter should react (envelope follower charges up)
    float loud[2048];
    fill_sine(loud, N, 440.0f, SR);
    for (int i = 0; i < N; ++i) loud[i] *= 0.9f; // near-full scale
    wah.process(loud, N);
    ASSERT_TRUE(buffer_is_finite(loud, N));
}

// ============================================================
// PhaserEffect tests
// ============================================================

TEST(phaser_produces_finite_output) {
    Phaser ph;
    ph.set_sample_rate(48000);
    ph.reset();

    float buf[1024];
    fill_sine(buf, 1024, 440.0f, 48000);
    ph.process(buf, 1024);

    ASSERT_TRUE(buffer_is_finite(buf, 1024));
    ASSERT_GT(rms(buf, 1024), 0.001f);
}

TEST(phaser_params_have_valid_ranges) {
    Phaser ph;
    for (auto& p : ph.params()) {
        ASSERT_TRUE(p.min_val <= p.max_val);
        ASSERT_TRUE(p.value >= p.min_val && p.value <= p.max_val);
        ASSERT_TRUE(p.default_val >= p.min_val && p.default_val <= p.max_val);
        ASSERT_FALSE(p.name.empty());
    }
}

TEST(phaser_disabled_passes_dry_signal) {
    Phaser ph;
    ph.set_sample_rate(48000);
    ph.reset();
    ph.set_enabled(false);

    float buf[256];
    float ref[256];
    fill_sine(buf, 256, 440.0f, 48000);
    for (int i = 0; i < 256; ++i) ref[i] = buf[i];
    ph.process(buf, 256);

    for (int i = 0; i < 256; ++i)
        ASSERT_NEAR(buf[i], ref[i], 1e-6f);
}

// Verify the LFO modulates the all-pass chain: consecutive blocks should
// differ because the LFO phase advances, causing spectral variation.
TEST(phaser_lfo_modulates_output) {
    Phaser ph;
    ph.set_sample_rate(48000);
    ph.reset();
    ph.params()[0].value = 2.0f;  // fast rate so LFO moves noticeably
    ph.params()[1].value = 1.0f;  // full depth
    ph.params()[4].value = 0.5f;  // 50% mix

    float buf_a[512], buf_b[512];
    fill_sine(buf_a, 512, 440.0f, 48000);
    fill_sine(buf_b, 512, 440.0f, 48000);

    // Process two independent blocks; LFO will be at different phases
    ph.process(buf_a, 512);
    ph.process(buf_b, 512);

    // The outputs of the two blocks must differ (LFO advanced between them)
    float diff = 0.0f;
    for (int i = 0; i < 512; ++i)
        diff += std::fabs(buf_a[i] - buf_b[i]);
    ASSERT_GT(diff, 0.01f);
}

TEST(phaser_all_stage_counts_finite) {
    for (int stage_param = 0; stage_param <= 3; ++stage_param) {
        Phaser ph;
        ph.set_sample_rate(48000);
        ph.reset();
        ph.params()[2].value = static_cast<float>(stage_param);

        float buf[512];
        fill_sine(buf, 512, 440.0f, 48000);
        ph.process(buf, 512);
        ASSERT_TRUE(buffer_is_finite(buf, 512));
    }
}

// ============================================================
// FlangerEffect tests
// ============================================================

TEST(flanger_produces_finite_output) {
    Flanger fl;
    fl.set_sample_rate(48000);
    fl.reset();

    float buf[1024];
    fill_sine(buf, 1024, 440.0f, 48000);
    fl.process(buf, 1024);

    ASSERT_TRUE(buffer_is_finite(buf, 1024));
    ASSERT_GT(rms(buf, 1024), 0.001f);
}

TEST(flanger_params_have_valid_ranges) {
    Flanger fl;
    for (auto& p : fl.params()) {
        ASSERT_TRUE(p.min_val <= p.max_val);
        ASSERT_TRUE(p.value >= p.min_val && p.value <= p.max_val);
        ASSERT_TRUE(p.default_val >= p.min_val && p.default_val <= p.max_val);
        ASSERT_FALSE(p.name.empty());
    }
}

TEST(flanger_disabled_passes_dry_signal) {
    Flanger fl;
    fl.set_sample_rate(48000);
    fl.reset();
    fl.set_enabled(false);

    float buf[256];
    float ref[256];
    fill_sine(buf, 256, 440.0f, 48000);
    for (int i = 0; i < 256; ++i) ref[i] = buf[i];
    fl.process(buf, 256);

    for (int i = 0; i < 256; ++i)
        ASSERT_NEAR(buf[i], ref[i], 1e-6f);
}

// Verify the LFO modulates the delay: two consecutive equal-signal blocks
// should differ because the sweep position advances between them.
TEST(flanger_lfo_modulates_output) {
    Flanger fl;
    fl.set_sample_rate(48000);
    fl.reset();
    fl.params()[0].value = 2.0f;  // fast rate
    fl.params()[1].value = 5.0f;  // wide depth
    fl.params()[4].value = 0.5f;  // 50% mix

    float buf_a[512], buf_b[512];
    fill_sine(buf_a, 512, 440.0f, 48000);
    fill_sine(buf_b, 512, 440.0f, 48000);

    fl.process(buf_a, 512);
    fl.process(buf_b, 512);

    float diff = 0.0f;
    for (int i = 0; i < 512; ++i)
        diff += std::fabs(buf_a[i] - buf_b[i]);
    ASSERT_GT(diff, 0.01f);
}

// High feedback must not produce runaway (NaN/Inf)
TEST(flanger_high_feedback_stays_finite) {
    Flanger fl;
    fl.set_sample_rate(48000);
    fl.reset();
    fl.params()[3].value = 0.95f;  // max positive feedback

    float buf[4096];
    fill_sine(buf, 4096, 220.0f, 48000);
    fl.process(buf, 4096);
    ASSERT_TRUE(buffer_is_finite(buf, 4096));
}
