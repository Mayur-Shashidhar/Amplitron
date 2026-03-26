#include "audio/effects/amp_simulator.h"

namespace GuitarAmp {

// ============================================================
// Factory amp model library
// ============================================================

const std::vector<AmpModel>& get_amp_models() {
    static const std::vector<AmpModel> models = {
        // ---------------------------------------------------------
        // Clean American — Fender Twin/Deluxe
        // Scooped mid Baxandall-style tone stack, gentle soft clipping,
        // glassy highs, warm lows. Designed for sparkling cleans.
        // ---------------------------------------------------------
        {
            "Clean American",
            "Fender Twin / Deluxe",
            "Sparkling clean, scooped mids, glassy highs",
            // Tone stack: bass boost, slight mid scoop, treble sparkle
            200.0f, 3.0f, 0.7f,     // low shelf: 200Hz +3dB
            800.0f, -2.0f, 0.8f,    // mid peak:  800Hz -2dB (scoop)
            3500.0f, 2.5f, 0.7f,    // high shelf: 3.5kHz +2.5dB
            // Saturation: very low gain, pure soft clip, symmetric
            1.2f,                     // preamp_gain
            0.0f,                     // saturation_mix (100% soft clip)
            1.0f,                     // asymmetry (symmetric)
            0.85f,                    // output_level
            // Dynamics: slow attack, medium release, no sag
            0.01f,                    // attack_coeff
            0.005f,                   // release_coeff
            0.0f,                     // sag_amount
        },
        // ---------------------------------------------------------
        // British Crunch — Marshall JCM800
        // Mid-forward tone stack, warm breakup with moderate gain,
        // asymmetric tube-like saturation. Classic rock crunch.
        // ---------------------------------------------------------
        {
            "British Crunch",
            "Marshall JCM800",
            "Warm breakup, mid-forward, classic rock crunch",
            // Tone stack: tight low end, mid push, smooth treble
            180.0f, -1.0f, 0.8f,    // low shelf: 180Hz -1dB (tight)
            650.0f, 4.0f, 1.2f,     // mid peak:  650Hz +4dB (mid push)
            3000.0f, 1.5f, 0.7f,    // high shelf: 3kHz +1.5dB
            // Saturation: moderate gain, mostly soft clip, asymmetric tubes
            3.5f,                     // preamp_gain
            0.15f,                    // saturation_mix (85% soft, 15% hard)
            0.8f,                     // asymmetry (tube-like asymmetry)
            0.7f,                     // output_level
            // Dynamics: medium attack, medium release, slight sag
            0.05f,                    // attack_coeff
            0.008f,                   // release_coeff
            0.15f,                    // sag_amount
        },
        // ---------------------------------------------------------
        // High Gain Modern — Mesa Boogie Rectifier
        // Tight low-end, scooped mids, aggressive hard clipping.
        // Dense harmonic saturation for modern metal.
        // ---------------------------------------------------------
        {
            "High Gain Modern",
            "Mesa Boogie Rectifier",
            "Tight low-end, scooped mids, aggressive distortion",
            // Tone stack: tight bass, deep mid scoop, present highs
            150.0f, 1.5f, 1.0f,     // low shelf: 150Hz +1.5dB (tight)
            500.0f, -4.5f, 0.9f,    // mid peak:  500Hz -4.5dB (scoop)
            4000.0f, 2.0f, 0.8f,    // high shelf: 4kHz +2dB
            // Saturation: high gain, hard clip dominant, slight asymmetry
            8.0f,                     // preamp_gain
            0.7f,                     // saturation_mix (30% soft, 70% hard)
            0.9f,                     // asymmetry
            0.55f,                    // output_level (lower to compensate gain)
            // Dynamics: fast attack, slow release, moderate sag
            0.15f,                    // attack_coeff
            0.003f,                   // release_coeff
            0.25f,                    // sag_amount
        },
        // ---------------------------------------------------------
        // Jazz Warm — Roland JC-120
        // Flat clean, warm highs rolloff, no breakup.
        // Designed for warm, round jazz tones.
        // ---------------------------------------------------------
        {
            "Jazz Warm",
            "Roland JC-120",
            "Flat clean, warm highs rolloff, round tone",
            // Tone stack: slight bass warmth, flat mids, rolled-off treble
            250.0f, 2.0f, 0.6f,     // low shelf: 250Hz +2dB (warmth)
            700.0f, 0.5f, 0.7f,     // mid peak:  700Hz +0.5dB (flat-ish)
            2800.0f, -3.5f, 0.6f,   // high shelf: 2.8kHz -3.5dB (rolloff)
            // Saturation: minimal gain, soft clip only, symmetric (solid state)
            1.0f,                     // preamp_gain
            0.0f,                     // saturation_mix (100% soft clip)
            1.0f,                     // asymmetry (symmetric — solid state)
            0.9f,                     // output_level
            // Dynamics: very slow attack, slow release, no sag
            0.005f,                   // attack_coeff
            0.003f,                   // release_coeff
            0.0f,                     // sag_amount
        },
    };
    return models;
}

// ============================================================
// AmpSimulator implementation
// ============================================================

AmpSimulator::AmpSimulator() {
    float max_model = static_cast<float>(get_amp_models().size() - 1);
    params_ = {
        {"Model", 0.0f, 0.0f, max_model, 0.0f, ""},     // Amp model selector
        {"Gain",  0.5f, 0.0f, 1.0f, 0.5f, ""},           // Extra gain control
        {"Bass",  0.0f, -6.0f, 6.0f, 0.0f, "dB"},        // Tone stack trim
        {"Mid",   0.0f, -6.0f, 6.0f, 0.0f, "dB"},        // Tone stack trim
        {"Treble", 0.0f, -6.0f, 6.0f, 0.0f, "dB"},       // Tone stack trim
        {"Level", 0.7f, 0.0f, 1.0f, 0.7f, ""},           // Master output level
    };
    set_sample_rate(DEFAULT_SAMPLE_RATE);
}

void AmpSimulator::set_sample_rate(int sample_rate) {
    Effect::set_sample_rate(sample_rate);
    cached_model_index_ = -1; // force recompute
    recompute_coefficients_if_dirty();
}

void AmpSimulator::compute_low_shelf(float freq, float gain_db, float q) {
    float A = std::pow(10.0f, gain_db / 40.0f);
    float w0 = TWO_PI * freq / sample_rate_;
    float cos_w0 = std::cos(w0);
    float sin_w0 = std::sin(w0);
    float alpha = sin_w0 / (2.0f * q);
    float sqA = std::sqrt(A);

    float a0 = (A + 1) + (A - 1) * cos_w0 + 2 * sqA * alpha;
    low_shelf_.b0 = (A * ((A + 1) - (A - 1) * cos_w0 + 2 * sqA * alpha)) / a0;
    low_shelf_.b1 = (2 * A * ((A - 1) - (A + 1) * cos_w0)) / a0;
    low_shelf_.b2 = (A * ((A + 1) - (A - 1) * cos_w0 - 2 * sqA * alpha)) / a0;
    low_shelf_.a1 = (-2 * ((A - 1) + (A + 1) * cos_w0)) / a0;
    low_shelf_.a2 = ((A + 1) + (A - 1) * cos_w0 - 2 * sqA * alpha) / a0;
}

void AmpSimulator::compute_peaking(float freq, float gain_db, float q) {
    float A = std::pow(10.0f, gain_db / 40.0f);
    float w0 = TWO_PI * freq / sample_rate_;
    float cos_w0 = std::cos(w0);
    float sin_w0 = std::sin(w0);
    float alpha = sin_w0 / (2.0f * q);

    float a0 = 1 + alpha / A;
    mid_peak_.b0 = (1 + alpha * A) / a0;
    mid_peak_.b1 = (-2 * cos_w0) / a0;
    mid_peak_.b2 = (1 - alpha * A) / a0;
    mid_peak_.a1 = (-2 * cos_w0) / a0;
    mid_peak_.a2 = (1 - alpha / A) / a0;
}

void AmpSimulator::compute_high_shelf(float freq, float gain_db, float q) {
    float A = std::pow(10.0f, gain_db / 40.0f);
    float w0 = TWO_PI * freq / sample_rate_;
    float cos_w0 = std::cos(w0);
    float sin_w0 = std::sin(w0);
    float alpha = sin_w0 / (2.0f * q);
    float sqA = std::sqrt(A);

    float a0 = (A + 1) - (A - 1) * cos_w0 + 2 * sqA * alpha;
    high_shelf_.b0 = (A * ((A + 1) + (A - 1) * cos_w0 + 2 * sqA * alpha)) / a0;
    high_shelf_.b1 = (-2 * A * ((A - 1) + (A + 1) * cos_w0)) / a0;
    high_shelf_.b2 = (A * ((A + 1) + (A - 1) * cos_w0 - 2 * sqA * alpha)) / a0;
    high_shelf_.a1 = (2 * ((A - 1) - (A + 1) * cos_w0)) / a0;
    high_shelf_.a2 = ((A + 1) - (A - 1) * cos_w0 - 2 * sqA * alpha) / a0;
}

void AmpSimulator::recompute_coefficients_if_dirty() {
    int model_idx = clamp(static_cast<int>(params_[0].value + 0.5f),
                          0, static_cast<int>(get_amp_models().size()) - 1);
    float bass_trim = params_[2].value;
    float mid_trim = params_[3].value;
    float treble_trim = params_[4].value;
    float gain_knob = params_[1].value;

    if (model_idx != cached_model_index_ ||
        bass_trim != cached_bass_ ||
        mid_trim != cached_mid_ ||
        treble_trim != cached_treble_ ||
        gain_knob != cached_gain_) {

        const AmpModel& model = get_amp_models()[model_idx];
        compute_low_shelf(model.bass_freq, model.bass_gain_db + bass_trim, model.bass_q);
        compute_peaking(model.mid_freq, model.mid_gain_db + mid_trim, model.mid_q);
        compute_high_shelf(model.treble_freq, model.treble_gain_db + treble_trim, model.treble_q);

        cached_model_index_ = model_idx;
        cached_bass_ = bass_trim;
        cached_mid_ = mid_trim;
        cached_treble_ = treble_trim;
        cached_gain_ = gain_knob;
    }
}

void AmpSimulator::process(float* buffer, int num_samples) {
    if (!enabled_) return;

    recompute_coefficients_if_dirty();

    int model_idx = clamp(static_cast<int>(params_[0].value + 0.5f),
                          0, static_cast<int>(get_amp_models().size()) - 1);
    const AmpModel& model = get_amp_models()[model_idx];

    float gain_knob = params_[1].value;
    float level = params_[5].value;

    // Effective preamp gain: model base * user gain control (0–2x range)
    float effective_gain = model.preamp_gain * (0.2f + gain_knob * 1.8f);
    float sat_mix = model.saturation_mix;
    float asym = model.asymmetry;
    float model_output = model.output_level;
    float attack = model.attack_coeff;
    float release = model.release_coeff;
    float sag = model.sag_amount;

    for (int i = 0; i < num_samples; ++i) {
        float dry = buffer[i];
        float x = buffer[i];

        // --- Envelope follower for dynamic response ---
        float abs_x = std::fabs(x);
        if (abs_x > envelope_) {
            envelope_ += attack * (abs_x - envelope_);
        } else {
            envelope_ += release * (abs_x - envelope_);
        }

        // Power sag: reduce gain when envelope is high (tube amp compression)
        float sag_factor = 1.0f - sag * clamp(envelope_, 0.0f, 1.0f);

        // --- Input gain with sag ---
        x *= effective_gain * sag_factor;

        // --- Tone stack (pre-saturation EQ) ---
        x = low_shelf_.process(x);
        x = mid_peak_.process(x);
        x = high_shelf_.process(x);

        // --- Waveshaping saturation ---
        // Soft clipping path (tube-like)
        float soft;
        if (x > 0.0f) {
            soft = 1.0f - std::exp(-x);
        } else {
            soft = -1.0f + std::exp(x);
            soft *= asym;
        }

        // Hard clipping path
        float hard = hard_clip(x, 1.0f);

        // Blend soft and hard clipping
        x = soft * (1.0f - sat_mix) + hard * sat_mix;

        // --- DC blocking high-pass filter ---
        float hp_coeff = 0.005f;
        hp_state_ += hp_coeff * (x - hp_state_);
        x -= hp_state_;

        // --- Output level ---
        x *= model_output * level;

        // Safety clamp
        x = clamp(x, -1.0f, 1.0f);

        // Wet/dry mix
        buffer[i] = dry * (1.0f - mix_) + x * mix_;
    }
}

void AmpSimulator::reset() {
    low_shelf_.reset();
    mid_peak_.reset();
    high_shelf_.reset();
    envelope_ = 0.0f;
    hp_state_ = 0.0f;
}

} // namespace GuitarAmp
