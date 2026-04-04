#pragma once

#include "audio/effect.h"

namespace GuitarAmp {

class Chorus : public Effect {
public:
    Chorus();
    void process(float* buffer, int num_samples) override;
    void process_stereo(float* left, float* right, int num_samples) override;
    void set_sample_rate(int sample_rate) override;
    void reset() override;
    const char* name() const override { return "Chorus"; }
    std::vector<EffectParam>& params() override { return params_; }

private:
    std::vector<EffectParam> params_;
    std::vector<float> delay_buffer_;
    int write_pos_ = 0;
    float lfo_phase_ = 0.0f;
    int max_delay_samples_ = 0;
};

} // namespace GuitarAmp
