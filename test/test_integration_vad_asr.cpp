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
import yspeech.op.feature.kaldi_fbank;
import yspeech.op.asr.paraformer;

using namespace yspeech;

namespace {

bool vad_model_exists() {
    std::ifstream file("model/vad/silero_vad.onnx");
    return file.good();
}

bool paraformer_model_exists() {
    std::ifstream model("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx");
    std::ifstream tokens("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt");
    return model.good() && tokens.good();
}

AudioFramePtr make_frame(const std::vector<float>& samples, std::uint64_t seq, bool eos = false) {
    static AudioFramePool pool;
    auto frame = pool.acquire(samples.size());
    frame->stream_id = "integration";
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
        float sample = 0.4f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * t);
        sample += 0.2f * std::sin(2.0f * static_cast<float>(M_PI) * 880.0f * t);
        audio.push_back(sample);
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

}

TEST(TestIntegrationVadAsr, VadAndFbankSmoke) {
    if (!vad_model_exists()) {
        GTEST_SKIP() << "VAD model not found";
    }

    OpSileroVad vad;
    vad.init({
        {"model_path", "model/vad/silero_vad.onnx"},
        {"input_frame_key", "audio_frames"},
        {"reader_key", "vad_reader"},
        {"output_key", "vad"},
        {"sample_rate", 16000}
    });

    OpKaldiFbank fbank;
    fbank.init({
        {"input_frame_key", "audio_frames"},
        {"reader_key", "fbank_reader"},
        {"output_key", "fbank"},
        {"sample_rate", 16000},
        {"num_bins", 80}
    });

    const auto audio = generate_audio(2.0f);
    Context ctx;
    StreamStore vad_store;
    vad_store.init_audio_ring("audio_frames", 4096);
    push_audio(vad_store, audio);

    while (vad.ready(ctx, vad_store)) {
        auto result = vad.process_stream(ctx, vad_store);
        if (result.status == StreamProcessStatus::NeedMoreInput) {
            break;
        }
    }

    StreamStore fbank_store;
    fbank_store.init_audio_ring("audio_frames", 4096);
    push_audio(fbank_store, audio);
    ASSERT_TRUE(fbank.ready(ctx, fbank_store));
    fbank.process_stream(ctx, fbank_store);
    EXPECT_TRUE(ctx.contains("fbank_features"));
}

TEST(TestIntegrationVadAsr, FbankToParaformerSmoke) {
    if (!paraformer_model_exists()) {
        GTEST_SKIP() << "Paraformer model not found";
    }

    OpKaldiFbank fbank;
    fbank.init({
        {"input_frame_key", "audio_frames"},
        {"reader_key", "fbank_reader"},
        {"output_key", "fbank"},
        {"sample_rate", 16000},
        {"num_bins", 80},
        {"lfr_window_size", 7},
        {"lfr_window_shift", 6}
    });

    OpAsrParaformer asr;
    asr.init({
        {"model_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx"},
        {"tokens_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt"},
        {"feature_input_key", "fbank"},
        {"output_key", "asr"}
    });

    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 4096);
    push_audio(store, generate_audio(3.0f));

    EXPECT_NO_THROW({
        fbank.process_stream(ctx, store);
        auto asr_result = asr.process_stream(ctx, store);
        EXPECT_TRUE(asr_result.status == StreamProcessStatus::NoOp ||
                    asr_result.status == StreamProcessStatus::ProducedOutput ||
                    asr_result.status == StreamProcessStatus::SegmentFinalized ||
                    asr_result.status == StreamProcessStatus::NeedMoreInput);
    });
}
