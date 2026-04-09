#include "gtest/gtest.h"
#include <nlohmann/json.hpp>

import std;
import yspeech;

TEST(TestEngine, TestEngine){
    EXPECT_NO_THROW(([] {
        nlohmann::json config = {
            {"name", "vad_only"},
            {"version", "1.0"},
            {"mode", "streaming"},
            {"frame", {{"sample_rate", 16000}, {"channels", 1}, {"dur_ms", 10}}},
            {"stream", {{"ring_capacity_frames", 128}}},
            {"pipeline", {{"push_chunk_samples", 1600}}},
            {"output", {{"type", "callback"}}},
            {"pipelines", {{{"id", "vad_stage"},
                            {"name", "VAD Stage"},
                            {"ops", {{{"id", "silero_vad"},
                                      {"name", "SileroVad"},
                                      {"params", {{"model_path", "model/vad/silero_vad.onnx"},
                                                  {"sample_rate", 16000},
                                                  {"input_frame_key", "audio_frames"}}}}}}}}}
        };
        yspeech::Engine engine(config);
    }()));
}


int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
