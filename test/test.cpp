#include "gtest/gtest.h"
#include <nlohmann/json.hpp>

import std;
import yspeech;
import yspeech.op;
import yspeech.stream_store;

TEST(TestEngine, TestOperatorIface){
    std::vector<yspeech::OperatorIface> operators;
    struct OpTest {
        OpTest() = default;
        ~OpTest() = default;

        OpTest(const OpTest&) = delete;
        OpTest& operator=(const OpTest&) = delete;
        OpTest(OpTest&&) noexcept = default;
        OpTest& operator=(OpTest&&) noexcept = default;
        
        void init(const nlohmann::json& config){
        }

        auto process_stream(yspeech::Context& ctx, yspeech::StreamStore&) -> yspeech::StreamProcessResult {
            ctx.set("op_test_processed", true);
            return {
                .status = yspeech::StreamProcessStatus::ProducedOutput,
                .wake_downstream = true
            };
        }
    };

    operators.emplace_back(OpTest());
    yspeech::Context ctx;
    yspeech::StreamStore store;
    store.init_audio_ring("audio_frames", 8);
    auto result = operators.front().process_stream(ctx, store);
    EXPECT_TRUE(ctx.get<bool>("op_test_processed"));
    EXPECT_EQ(result.status, yspeech::StreamProcessStatus::ProducedOutput);
}

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
