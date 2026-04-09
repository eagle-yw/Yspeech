#include "gtest/gtest.h"
#include <nlohmann/json.hpp>

import yspeech;
import yspeech.op;
import yspeech.stream_store;
import std;

namespace {

struct TestNoopOp {
    void init(const nlohmann::json&) {
    }

    auto process_stream(yspeech::Context& ctx, yspeech::StreamStore&) -> yspeech::StreamProcessResult {
        ctx.set("test_noop_processed", true);
        return {
            .status = yspeech::StreamProcessStatus::ProducedOutput,
            .wake_downstream = true
        };
    }
};

yspeech::OperatorRegistrar<TestNoopOp> test_noop_registrar("TestNoopOp");

} // namespace

TEST(TestPipeline, TestBuildAndRun) {
    yspeech::PipelineManager pipeline;
    
    std::string config_path = "temp_config.json";
    std::ofstream out(config_path);
    out << R"({
      "pipelines": [
        {
          "id": "test_stage",
          "ops": [
            {
              "id": "noop_op",
              "name": "TestNoopOp",
              "depends_on": []
            }
          ]
        }
      ]
    })";
    out.close();

    EXPECT_NO_THROW(pipeline.build(config_path));
    
    yspeech::Context ctx;
    yspeech::StreamStore store;
    store.init_audio_ring("audio_frames", 8);
    EXPECT_NO_THROW(pipeline.run_stream(ctx, store, false));
    EXPECT_NO_THROW(pipeline.run_stream(ctx, store, true));
    EXPECT_TRUE(ctx.get<bool>("test_noop_processed"));
    
    std::remove(config_path.c_str());
}
