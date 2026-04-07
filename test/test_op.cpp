#include "gtest/gtest.h"
#include <nlohmann/json.hpp>

import yspeech;
import yspeech.op.vad;
import yspeech.op;
import std;

TEST(TestPipeline, TestBuildAndRun) {
    yspeech::PipelineManager pipeline;
    
    std::string config_path = "temp_config.json";
    std::ofstream out(config_path);
    out << R"({
      "ops": [
        {
          "id": "vad_op",
          "name": "Vad",
          "params": {
            "model_path": "dummy_path"
          },
          "depends_on": []
        }
      ]
    })";
    out.close();

    EXPECT_NO_THROW(pipeline.build(config_path));
    
    yspeech::Context ctx;
    EXPECT_NO_THROW(pipeline.run(ctx));
    
    std::remove(config_path.c_str());
}

TEST(TestOperator, TestVadInit) {
    yspeech::OpVad op;
    nlohmann::json config = {{"model_path", "dummy"}};
    EXPECT_NO_THROW(op.init(config));
}
