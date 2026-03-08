#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>

import yspeech;
import std;

using namespace yspeech;

class MockAspect {
public:
    MOCK_METHOD(std::any, before, (Context& ctx, const std::string& op_name));
    MOCK_METHOD(void, after, (Context& ctx, const std::string& op_name, std::any payload));
};

TEST(AspectTest, BasicFlow) {
    std::string config_path = "test_aspect_config.json";
    std::ofstream f(config_path);
    f << R"({
        "ops": [
            { "id": "op1", "name": "Vad", "params": { "model_path": "dummy" } }
        ]
    })";
    f.close();

    Pipeline pipeline;
    auto mock_aspect = std::make_shared<MockAspect>();
    
    EXPECT_CALL(*mock_aspect, before(testing::_, "op1"))
        .Times(1)
        .WillOnce(testing::Return(std::any(42)));

    EXPECT_CALL(*mock_aspect, after(testing::_, "op1", testing::_))
        .Times(1)
        .WillOnce([](Context&, const std::string&, std::any payload) {
            EXPECT_EQ(std::any_cast<int>(payload), 42);
        });

    struct MockAspectWrapper {
        std::shared_ptr<MockAspect> mock;
        auto before(Context& ctx, const std::string& op_name) -> std::any { 
            return mock->before(ctx, op_name); 
        }
        auto after(Context& ctx, const std::string& op_name, std::any payload) -> void { 
            mock->after(ctx, op_name, std::move(payload)); 
        }
    };

    pipeline.add_aspect(MockAspectWrapper{mock_aspect});
    pipeline.build(config_path);
    Context ctx;
    pipeline.run(ctx);

    std::filesystem::remove(config_path);
}

TEST(AspectTest, MultipleAspectsOrder) {
    std::string config_path = "test_aspect_order.json";
    std::ofstream f(config_path);
    f << R"({
        "ops": [ { "id": "op1", "name": "Vad" } ]
    })";
    f.close();

    Pipeline pipeline;
    std::vector<std::string> order;

    struct AspectA {
        std::vector<std::string>* order;
        auto before(Context&, const std::string&) -> std::any { 
            order->push_back("A_before"); 
            return std::any(); 
        }
        auto after(Context&, const std::string&, std::any) -> void { 
            order->push_back("A_after"); 
        }
    };

    struct AspectB {
        std::vector<std::string>* order;
        auto before(Context&, const std::string&) -> std::any { 
            order->push_back("B_before"); 
            return std::any(); 
        }
        auto after(Context&, const std::string&, std::any) -> void { 
            order->push_back("B_after"); 
        }
    };

    pipeline.add_aspect(AspectA{&order});
    pipeline.add_aspect(AspectB{&order});

    pipeline.build(config_path);
    Context ctx;
    pipeline.run(ctx);

    std::vector<std::string> expected = {"A_before", "B_before", "B_after", "A_after"};
    EXPECT_EQ(order, expected);

    std::filesystem::remove(config_path);
}
