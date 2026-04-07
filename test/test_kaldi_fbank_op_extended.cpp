#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <cmath>
#include <fstream>

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
    frame->stream_id = "fbank_extended";
    frame->seq = seq;
    frame->sample_rate = 16000;
    frame->channels = 1;
    frame->pts_ms = static_cast<std::int64_t>(seq * 10);
    frame->dur_ms = 10;
    frame->eos = eos;
    frame->samples = samples;
    return frame;
}

std::vector<float> generate_audio(float duration_sec, float dc_offset = 0.0f) {
    std::vector<float> audio;
    const int sample_rate = 16000;
    const int num_samples = static_cast<int>(duration_sec * static_cast<float>(sample_rate));
    audio.reserve(static_cast<std::size_t>(num_samples));
    for (int i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        audio.push_back(0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * t) + dc_offset);
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

TEST(TestKaldiFbankExtended, WindowTypes) {
    for (const auto* window_type : {"povey", "hamming", "hanning"}) {
        auto fbank = create_fbank({{"window_type", window_type}});
        Context ctx;
        StreamStore store;
        store.init_audio_ring("audio_frames", 2048);
        push_audio(store, generate_audio(1.0f));
        EXPECT_NO_THROW(fbank.process_stream(ctx, store));
        EXPECT_TRUE(ctx.contains("fbank_features"));
    }
}

TEST(TestKaldiFbankExtended, PreemphasisAndDcOffset) {
    auto fbank = create_fbank({
        {"preemph_coeff", 0.97f},
        {"remove_dc_offset", true}
    });
    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 4096);
    push_audio(store, generate_audio(2.0f, 0.5f));
    fbank.process_stream(ctx, store);
    EXPECT_TRUE(ctx.contains("fbank_features"));
}

TEST(TestKaldiFbankExtended, EmptyAndShortAudio) {
    auto fbank = create_fbank();
    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 32);
    EXPECT_EQ(fbank.process_stream(ctx, store).status, StreamProcessStatus::NeedMoreInput);

    push_audio(store, generate_audio(0.02f));
    EXPECT_NO_THROW(fbank.process_stream(ctx, store));
}

TEST(TestKaldiFbankExtended, FeatureValuesReasonable) {
    auto fbank = create_fbank();
    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 4096);
    push_audio(store, generate_audio(2.0f));
    fbank.process_stream(ctx, store);

    auto features = ctx.get<std::vector<std::vector<float>>>("fbank_features");
    ASSERT_FALSE(features.empty());
    for (const auto& frame : features) {
        for (float value : frame) {
            EXPECT_TRUE(std::isfinite(value));
            EXPECT_LT(value, 20.0f);
            EXPECT_GT(value, -50.0f);
        }
    }
}

TEST(TestKaldiFbankExtended, MoveSemantics) {
    auto fbank1 = create_fbank();
    OpKaldiFbank fbank2 = std::move(fbank1);
    EXPECT_EQ(fbank2.feature_dim(), 80);

    OpKaldiFbank fbank3;
    fbank3 = std::move(fbank2);
    EXPECT_EQ(fbank3.feature_dim(), 80);
}
