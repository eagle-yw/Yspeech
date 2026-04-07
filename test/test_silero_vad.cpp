#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cmath>

import std;
import yspeech.context;
import yspeech.stream_store;
import yspeech.types;
import yspeech.op;
import yspeech.op.vad.silero;

using namespace yspeech;

namespace {

bool model_exists() {
    std::ifstream file("model/vad/silero_vad.onnx");
    return file.good();
}

AudioFramePtr make_frame(const std::vector<float>& samples, std::uint64_t seq, bool eos = false) {
    static AudioFramePool pool;
    auto frame = pool.acquire(samples.size());
    frame->stream_id = "vad_test";
    frame->seq = seq;
    frame->sample_rate = 16000;
    frame->channels = 1;
    frame->pts_ms = static_cast<std::int64_t>(seq * 10);
    frame->dur_ms = 10;
    frame->eos = eos;
    frame->samples = samples;
    return frame;
}

std::vector<float> make_audio(std::size_t samples, float scale = 0.1f) {
    std::vector<float> audio(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        audio[i] = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * static_cast<float>(i) / 16000.0f) * scale;
    }
    return audio;
}

void push_frames(StreamStore& store, const std::vector<float>& audio) {
    constexpr std::size_t frame_samples = 160;
    std::uint64_t seq = 0;
    for (std::size_t offset = 0; offset < audio.size(); offset += frame_samples, ++seq) {
        auto end = std::min(offset + frame_samples, audio.size());
        std::vector<float> frame_samples_buffer(audio.begin() + static_cast<std::ptrdiff_t>(offset),
                                                audio.begin() + static_cast<std::ptrdiff_t>(end));
        store.push_frame("audio_frames", make_frame(frame_samples_buffer, seq, end == audio.size()));
    }
}

OpSileroVad create_vad(float threshold = 0.5f) {
    OpSileroVad vad;
    nlohmann::json config = {
        {"model_path", "model/vad/silero_vad.onnx"},
        {"threshold", threshold},
        {"sample_rate", 16000},
        {"input_frame_key", "audio_frames"},
        {"output_key", "vad"}
    };
    vad.init(config);
    return vad;
}

}

TEST(TestSileroVad, BasicInit) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }
    EXPECT_NO_THROW(create_vad());
}

TEST(TestSileroVad, ProcessEmptyStore) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }

    auto vad = create_vad();
    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 64);

    auto result = vad.process_stream(ctx, store);
    EXPECT_EQ(result.status, StreamProcessStatus::NeedMoreInput);
    EXPECT_FALSE(ctx.contains("vad_probability"));
}

TEST(TestSileroVad, ProcessWithAudioData) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }

    auto vad = create_vad(0.5f);
    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 512);
    push_frames(store, make_audio(5120));

    while (vad.ready(ctx, store)) {
        auto result = vad.process_stream(ctx, store);
        if (result.status == StreamProcessStatus::NeedMoreInput) {
            break;
        }
    }

    if (ctx.contains("vad_probability")) {
        float prob = ctx.get<float>("vad_probability");
        EXPECT_GE(prob, 0.0f);
        EXPECT_LE(prob, 1.0f);
    }
}

TEST(TestSileroVad, StateManagement) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }

    auto vad = create_vad();
    EXPECT_FALSE(vad.is_speech());
    EXPECT_FLOAT_EQ(vad.current_probability(), 0.0f);
}

TEST(TestSileroVad, Deinit) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }

    auto vad = create_vad();
    EXPECT_NO_THROW(vad.deinit());
}
