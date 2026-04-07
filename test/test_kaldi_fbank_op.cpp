#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <cmath>

import std;
import yspeech.context;
import yspeech.stream_store;
import yspeech.types;
import yspeech.op;
import yspeech.op.feature.kaldi_fbank;

using namespace yspeech;

namespace {

AudioFramePtr make_frame(const std::vector<float>& samples, std::uint64_t seq, bool eos = false) {
    static AudioFramePool pool;
    auto frame = pool.acquire(samples.size());
    frame->stream_id = "fbank_test";
    frame->seq = seq;
    frame->sample_rate = 16000;
    frame->channels = 1;
    frame->pts_ms = static_cast<std::int64_t>(seq * 10);
    frame->dur_ms = 10;
    frame->eos = eos;
    frame->samples = samples;
    return frame;
}

std::vector<float> generate_audio(float duration_sec, float freq = 440.0f) {
    std::vector<float> audio;
    const int sample_rate = 16000;
    const int num_samples = static_cast<int>(duration_sec * static_cast<float>(sample_rate));
    audio.reserve(static_cast<std::size_t>(num_samples));
    for (int i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        audio.push_back(0.5f * std::sin(2.0f * static_cast<float>(M_PI) * freq * t));
    }
    return audio;
}

void push_audio(StreamStore& store, const std::vector<float>& audio) {
    constexpr std::size_t frame_samples = 160;
    std::uint64_t seq = 0;
    for (std::size_t offset = 0; offset < audio.size(); offset += frame_samples, ++seq) {
        const auto end = std::min(offset + frame_samples, audio.size());
        std::vector<float> chunk(audio.begin() + static_cast<std::ptrdiff_t>(offset),
                                 audio.begin() + static_cast<std::ptrdiff_t>(end));
        store.push_frame("audio_frames", make_frame(chunk, seq, end == audio.size()));
    }
}

OpKaldiFbank create_fbank(const nlohmann::json& extra = {}) {
    OpKaldiFbank fbank;
    nlohmann::json config = {
        {"sample_rate", 16000},
        {"num_bins", 80},
        {"frame_length_ms", 25.0f},
        {"frame_shift_ms", 10.0f},
        {"input_frame_key", "audio_frames"},
        {"output_key", "fbank"}
    };
    for (const auto& [key, value] : extra.items()) {
        config[key] = value;
    }
    fbank.init(config);
    return fbank;
}

}

TEST(TestKaldiFbankOp, BasicInit) {
    auto fbank = create_fbank();
    EXPECT_EQ(fbank.feature_dim(), 80);
    EXPECT_FLOAT_EQ(fbank.frame_shift(), 0.01f);
}

TEST(TestKaldiFbankOp, ExtractFeatures) {
    auto fbank = create_fbank();
    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 4096);
    push_audio(store, generate_audio(3.0f));

    ASSERT_TRUE(fbank.ready(ctx, store));
    auto result = fbank.process_stream(ctx, store);

    EXPECT_NE(result.status, StreamProcessStatus::NeedMoreInput);
    EXPECT_TRUE(ctx.contains("fbank_features"));
    EXPECT_TRUE(ctx.contains("fbank_num_frames"));
    EXPECT_TRUE(ctx.contains("fbank_num_bins"));
    EXPECT_GT(ctx.get<int>("fbank_num_frames"), 0);
    EXPECT_EQ(ctx.get<int>("fbank_num_bins"), 80);
}

TEST(TestKaldiFbankOp, DifferentConfigs) {
    auto fbank40 = create_fbank({{"num_bins", 40}});
    EXPECT_EQ(fbank40.feature_dim(), 40);

    auto fast_shift = create_fbank({
        {"frame_length_ms", 20.0f},
        {"frame_shift_ms", 5.0f},
        {"output_key", "fbank_fast"}
    });
    EXPECT_FLOAT_EQ(fast_shift.frame_shift(), 0.005f);
}

TEST(TestKaldiFbankOp, DifferentFrequencies) {
    for (float freq : {100.0f, 440.0f, 1000.0f, 4000.0f}) {
        auto fbank = create_fbank();
        Context ctx;
        StreamStore store;
        store.init_audio_ring("audio_frames", 2048);
        push_audio(store, generate_audio(0.5f, freq));
        fbank.process_stream(ctx, store);
        EXPECT_TRUE(ctx.contains("fbank_features")) << freq;
    }
}

TEST(TestKaldiFbankOp, LowFrameRateReduction) {
    auto fbank = create_fbank({
        {"lfr_window_size", 7},
        {"lfr_window_shift", 6}
    });
    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 4096);
    push_audio(store, generate_audio(1.0f));
    fbank.process_stream(ctx, store);
    EXPECT_EQ(ctx.get<int>("fbank_num_bins"), 560);
}

TEST(TestKaldiFbankOp, Deinit) {
    auto fbank = create_fbank();
    EXPECT_NO_THROW(fbank.deinit());
}
