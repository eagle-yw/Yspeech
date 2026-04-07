#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <cmath>
#include <chrono>

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
    frame->stream_id = "fbank_perf";
    frame->seq = seq;
    frame->sample_rate = 16000;
    frame->channels = 1;
    frame->pts_ms = static_cast<std::int64_t>(seq * 10);
    frame->dur_ms = 10;
    frame->eos = eos;
    frame->samples = samples;
    return frame;
}

std::vector<float> generate_audio(float duration_sec) {
    std::vector<float> audio;
    const int sample_rate = 16000;
    const int num_samples = static_cast<int>(duration_sec * static_cast<float>(sample_rate));
    audio.reserve(static_cast<std::size_t>(num_samples));
    for (int i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        audio.push_back(0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * t));
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

OpKaldiFbank create_fbank() {
    OpKaldiFbank fbank;
    nlohmann::json config = {
        {"sample_rate", 16000},
        {"num_bins", 80},
        {"input_frame_key", "audio_frames"},
        {"output_key", "fbank"}
    };
    fbank.init(config);
    return fbank;
}

}

TEST(TestKaldiFbankPerformance, BenchmarkFeatureExtraction) {
    auto fbank = create_fbank();
    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 8192);
    push_audio(store, generate_audio(10.0f));

    const auto begin = std::chrono::steady_clock::now();
    while (fbank.ready(ctx, store)) {
        auto result = fbank.process_stream(ctx, store);
        if (result.status == StreamProcessStatus::NeedMoreInput) {
            break;
        }
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin).count();

    EXPECT_TRUE(ctx.contains("fbank_num_frames"));
    EXPECT_GT(ctx.get<int>("fbank_num_frames"), 0);
    EXPECT_LT(elapsed_ms, 5000);
}

TEST(TestKaldiFbankPerformance, FlushFinalization) {
    auto fbank = create_fbank();
    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 1024);
    push_audio(store, generate_audio(1.0f));

    fbank.process_stream(ctx, store);
    auto result = fbank.flush(ctx, store);
    EXPECT_TRUE(result.status == StreamProcessStatus::NoOp ||
                result.status == StreamProcessStatus::StreamFinalized);
}
