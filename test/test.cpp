#include "gtest/gtest.h"

import std;
import yspeech;



TEST(TestEngine, TestOperatorIface){
    std::vector<yspeech::OperatorIface> operators;
    struct OpTest {
        OpTest() {
            std::cout << "OpTest" << std::endl;
        }
        ~OpTest() {
            std::cout << "~OpTest" << std::endl;
        }

        OpTest(const OpTest&) = delete;
        OpTest& operator=(const OpTest&) = delete;
        OpTest(OpTest&&) noexcept = default;
        OpTest& operator=(OpTest&&) noexcept = default;
        
        void load(std::string_view path){

        }

        void process(yspeech::Context bytes){

        }
    };

    operators.emplace_back(yspeech::OpVad());
    operators.emplace_back(OpTest());
}

TEST(TestEngine, TestEngine){
    auto engine = yspeech::Engine();
    engine.run();
}


int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}