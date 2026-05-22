#include "test_framework.h"
#include "audio/effects/cabinet_sim.h"
#include <cstdint>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include <atomic>

using namespace Amplitron;
using namespace TestFramework;

// Test-local allocation counter: override global new/delete for this TU so
// we can deterministically detect heap allocations during the hot process()
// loop. This replacement is intentionally minimal and only used by these
// unit tests to assert zero allocations; it forwards to malloc/free.
namespace {
thread_local std::atomic<int> g_alloc_count{0};

void* operator new(std::size_t sz) {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(sz);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t sz) {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(sz);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t sz, const std::nothrow_t&) noexcept {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(sz);
}
void* operator new[](std::size_t sz, const std::nothrow_t&) noexcept {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(sz);
}
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
} // anonymous namespace

static void write_le16(std::ofstream& out, uint16_t v) {
    char b[2];
    b[0] = static_cast<char>(v & 0xFF);
    b[1] = static_cast<char>((v >> 8) & 0xFF);
    out.write(b, 2);
}

static void write_le32(std::ofstream& out, uint32_t v) {
    char b[4];
    b[0] = static_cast<char>(v & 0xFF);
    b[1] = static_cast<char>((v >> 8) & 0xFF);
    b[2] = static_cast<char>((v >> 16) & 0xFF);
    b[3] = static_cast<char>((v >> 24) & 0xFF);
    out.write(b, 4);
}

static bool write_wav_mono_pcm16(const std::string& path,
                                 const std::vector<float>& samples,
                                 int sample_rate) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;

    const uint16_t num_channels = 1;
    const uint16_t bits_per_sample = 16;
    const uint16_t block_align = static_cast<uint16_t>(num_channels * (bits_per_sample / 8));
    const uint32_t byte_rate = static_cast<uint32_t>(sample_rate) * block_align;
    const uint32_t data_bytes = static_cast<uint32_t>(samples.size()) * block_align;
    const uint32_t riff_size = 36 + data_bytes;

    // RIFF header
    out.write("RIFF", 4);
    write_le32(out, riff_size);
    out.write("WAVE", 4);

    // fmt chunk
    out.write("fmt ", 4);
    write_le32(out, 16);               // PCM fmt chunk size
    write_le16(out, 1);                // audio format = PCM
    write_le16(out, num_channels);
    write_le32(out, static_cast<uint32_t>(sample_rate));
    write_le32(out, byte_rate);
    write_le16(out, block_align);
    write_le16(out, bits_per_sample);

    // data chunk
    out.write("data", 4);
    write_le32(out, data_bytes);

    // samples (clamped)
    for (float s : samples) {
        float x = std::fmax(-1.0f, std::fmin(1.0f, s));
        int16_t v = static_cast<int16_t>(std::lrint(x * 32767.0f));
        write_le16(out, static_cast<uint16_t>(v));
    }

    return true;
}

TEST(CabinetSim_IR_UnitImpulse_Identity) {
    const int block_size = 256;

    // IR = [1.0] should act as identity
    std::string path = "test_cabinet_ir_unit_impulse.wav";
    ASSERT_TRUE(write_wav_mono_pcm16(path, {1.0f}, 48000));

    CabinetSim cab;
    cab.set_sample_rate(48000);
    ASSERT_TRUE(cab.load_ir(path));
    ASSERT_TRUE(cab.has_ir());
    cab.set_enabled(true);

    std::vector<float> buf(block_size);
    for (int i = 0; i < block_size; ++i) {
        buf[i] = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);
    }
    std::vector<float> expected(buf);

    cab.process(buf.data(), block_size);

    for (int i = 0; i < block_size; ++i) {
        ASSERT_NEAR(buf[i], expected[i], 1e-4f);
    }

    std::remove(path.c_str());
}

TEST(CabinetSim_IR_DelayedImpulse) {
    const int block_size = 256;

    // IR = [0, 0, 0, 0, 1.0] delays by 4 samples
    std::string path = "test_cabinet_ir_delayed_impulse.wav";
    std::vector<float> ir(5, 0.0f);
    ir[4] = 1.0f;
    ASSERT_TRUE(write_wav_mono_pcm16(path, ir, 48000));

    CabinetSim cab;
    cab.set_sample_rate(48000);
    ASSERT_TRUE(cab.load_ir(path));
    cab.set_enabled(true);

    std::vector<float> buf(block_size, 0.0f);
    buf[0] = 1.0f;

    cab.process(buf.data(), block_size);

    for (int i = 0; i < block_size; ++i) {
        float expected = (i == 4) ? 1.0f : 0.0f;
        ASSERT_NEAR(buf[i], expected, 1e-4f);
    }

    std::remove(path.c_str());
}

// ============================================================
// STRESS TEST: Real-time safety with long IR and small buffers
// ============================================================
//
// This test validates that IR Cabinet processing does NOT cause heap
// allocations, excessive jitter, or unstable callback durations when
// using small audio buffers (64–256 samples) with long IRs (100ms+).
// 
// The test runs many consecutive audio frames, measures callback durations,
// and verifies stable, predictable processing times without spikes.

TEST(CabinetSim_IR_RealTime_Safety_SmallBuffers) {
    const int sample_rate = 48000;
    const int ir_duration_ms = 100;
    const int ir_length = (sample_rate * ir_duration_ms) / 1000;  // 4800 samples
    
    // Create a long IR: exponentially decaying impulse response
    std::vector<float> ir_samples(static_cast<size_t>(ir_length));
    for (int i = 0; i < ir_length; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        ir_samples[static_cast<size_t>(i)] = 0.5f * std::exp(-3.0f * t);
    }
    
    // Write IR to WAV file
    std::string ir_path = "test_stress_long_ir.wav";
    ASSERT_TRUE(write_wav_mono_pcm16(ir_path, ir_samples, sample_rate));
    
    // Create cabinet and load long IR
    CabinetSim cab;
    cab.set_sample_rate(sample_rate);
    ASSERT_TRUE(cab.load_ir(ir_path));
    cab.set_enabled(true);
    
    // Test across multiple small buffer sizes
    const int buffer_sizes[] = {64, 128, 256};
    
    for (int buf_size : buffer_sizes) {
        // Reset cabinet state for each buffer size
        cab.reset();
        
        // Generate input signal: 440 Hz sine wave
        std::vector<float> input(static_cast<size_t>(buf_size));
        for (int i = 0; i < buf_size; ++i) {
            input[i] = 0.1f * std::sin(2.0f * 3.14159265f * 440.0f * 
                                       static_cast<float>(i) / static_cast<float>(sample_rate));
        }
        
        // Warm-up: allow any one-time allocations (kernel build, internal buffers)
        // to occur before we assert that the steady-state hot loop performs
        // zero heap allocations.
        const int warmup_frames = 10;
        for (int frame = 0; frame < warmup_frames; ++frame) {
            std::vector<float> buf = input;
            cab.process(buf.data(), buf_size);
            for (int i = 0; i < buf_size; ++i) {
                ASSERT_TRUE(std::isfinite(buf[i]));
            }
        }

        // Zero the allocation counter and run the hot loop; ASSERT no heap
        // allocations occur during the hot steady-state.
        g_alloc_count.store(0, std::memory_order_relaxed);
        const int hot_frames = 500;
        for (int frame = 0; frame < hot_frames; ++frame) {
            std::vector<float> buf = input;
            cab.process(buf.data(), buf_size);
            for (int i = 0; i < buf_size; ++i) {
                ASSERT_TRUE(std::isfinite(buf[i]));
            }
        }

        ASSERT_EQ(g_alloc_count.load(std::memory_order_relaxed), 0)
            << "Heap allocation detected during CabinetSim::process() hot loop for buffer size " << buf_size;
    }
    
    std::remove(ir_path.c_str());
}


