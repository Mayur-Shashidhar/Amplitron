#pragma once

#include "audio/effect.h"
#include <array>

namespace GuitarAmp {

/**
 * Phaser effect — cascaded 1st-order all-pass filters with LFO modulation.
 * Supports 4, 6, 8, or 12 stages (classic MXR Phase 90 to studio phasers).
 */
class Phaser : public Effect {
public:
    Phaser();
    void process(float* buffer, int num_samples) override;
    void set_sample_rate(int sample_rate) override;
    void reset() override;
    const char* name() const override { return "Phaser"; }
    std::vector<EffectParam>& params() override { return params_; }

private:
    std::vector<EffectParam> params_;

    float lfo_phase_ = 0.0f;
    float feedback_state_ = 0.0f;  // last sample out of the all-pass chain

    static constexpr int MAX_STAGES = 12;
    std::array<float, MAX_STAGES> apf_xprev_{};
    std::array<float, MAX_STAGES> apf_yprev_{};
};

} // namespace GuitarAmp
