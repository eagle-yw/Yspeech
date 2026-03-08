#include "gtest/gtest.h"
#include <nlohmann/json.hpp>

import std;
import yspeech;
import yspeech.op;

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

        void process(yspeech::Context& bytes){
        }
    };

    operators.emplace_back(OpTest());
}

TEST(TestEngine, TestEngine){
    auto engine = yspeech::Engine();
    yspeech::Context ctx;
    engine.run(ctx);
}


int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
