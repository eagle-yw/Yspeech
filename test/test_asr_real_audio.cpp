#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>

import std;
import yspeech;

namespace fs = std::filesystem;

namespace {

bool paraformer_assets_exist() {
    std::ifstream model("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx");
    std::ifstream tokens("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt");
    return model.good() && tokens.good();
}

bool streaming_config_exists() {
    return fs::exists("examples/configs/streaming_paraformer_asr.json");
}

bool offline_config_exists() {
    return fs::exists("examples/configs/offline_paraformer_asr.json");
}

fs::path sample_audio_path() {
    fs::path path("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav");
    if (fs::exists(path)) {
        return path;
    }
    return {};
}

}

TEST(TestAsrRealAudio, OfflineEngineSmoke) {
    const auto audio_path = sample_audio_path();
    if (audio_path.empty() || !offline_config_exists() || !paraformer_assets_exist()) {
        GTEST_SKIP() << "Offline demo assets not found";
    }

    yspeech::EngineConfigOptions options;
    options.audio_path = audio_path.string();
    options.playback_rate = 1.0;
    yspeech::Engine engine(std::string("examples/configs/offline_paraformer_asr.json"), options);

    std::vector<yspeech::EngineEvent> events;
    engine.on_event([&](const yspeech::EngineEvent& event) { events.push_back(event); });

    engine.start();
    engine.finish();
    while (!engine.input_eof_reached()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    engine.stop();

    EXPECT_FALSE(events.empty());
}

TEST(TestAsrRealAudio, StreamingEngineSmoke) {
    const auto audio_path = sample_audio_path();
    if (audio_path.empty() || !streaming_config_exists() || !paraformer_assets_exist()) {
        GTEST_SKIP() << "Streaming demo assets not found";
    }

    yspeech::EngineConfigOptions options;
    options.audio_path = audio_path.string();
    options.playback_rate = 1.0;
    yspeech::Engine engine(std::string("examples/configs/streaming_paraformer_asr.json"), options);

    std::vector<yspeech::EngineEvent> events;
    engine.on_event([&](const yspeech::EngineEvent& event) { events.push_back(event); });

    engine.start();
    engine.finish();
    while (!engine.input_eof_reached()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    engine.stop();

    EXPECT_FALSE(events.empty());
}

TEST(TestAsrRealAudio, ExplicitFrameSourceOverridesConfigSource) {
    const auto audio_path = sample_audio_path();
    if (audio_path.empty() || !streaming_config_exists() || !paraformer_assets_exist()) {
        GTEST_SKIP() << "Streaming demo assets not found";
    }

    yspeech::EngineConfigOptions options;
    options.audio_path = audio_path.string();
    options.playback_rate = 0.01;
    yspeech::Engine engine(std::string("examples/configs/streaming_paraformer_asr.json"), options);

    auto override_source = std::make_shared<yspeech::MicSource>("override");
    override_source->push_frame(override_source->make_frame({}, 0, 0, true, false, 16000, 1));
    auto pipeline_source = std::make_shared<yspeech::AudioFramePipelineSource>(override_source);
    engine.set_frame_source(pipeline_source);

    engine.start();
    engine.finish();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (!engine.input_eof_reached() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    engine.stop();

    EXPECT_TRUE(engine.input_eof_reached());
}
