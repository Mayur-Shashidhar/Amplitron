#pragma once

#include "audio/effect.h"

namespace GuitarAmp {

/**
 * Flanger effect — short modulated delay line (0.1–15ms) mixed with dry signal.
 * Feedback through the delay line creates the classic "comb filter sweep" sound.
 */
class Flanger : public Effect {
public:
    Flanger();
    void process(float* buffer, int num_samples) override;
    void process_stereo(float* left, float* right, int num_samples) override;
    void set_sample_rate(int sample_rate) override;
    void reset() override;
    const char* name() const override { return "Flanger"; }
    std::vector<EffectParam>& params() override { return params_; }

private:
    std::vector<EffectParam> params_;
    std::vector<float> delay_buffer_;
    int write_pos_ = 0;
    float lfo_phase_ = 0.0f;
    int max_delay_samples_ = 0;
    // Right-channel delay line — LFO is 180° out of phase
    std::vector<float> delay_buffer_r_;
    int write_pos_r_ = 0;
};

} // namespace GuitarAmp
