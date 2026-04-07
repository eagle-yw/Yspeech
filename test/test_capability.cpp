#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <yspeech/register.hpp>

import yspeech;
import std;

using namespace yspeech;
using json = nlohmann::json;

namespace {

struct StreamTestOp {
    void init(const json&) {}
    StreamProcessResult process_stream(Context& ctx, StreamStore&) {
        ctx.set("op_processed", true);
        return {
            .status = StreamProcessStatus::ProducedOutput,
            .wake_downstream = true
        };
    }
};

}

class PreCapability {
public:
    PreCapability(const json& config = {}) : config_(config) {}
    
    void init(const json& config) { config_ = config; }
    
    void apply(Context& ctx) {
        ctx.set("pre_applied", true);
        ctx.set("pre_value", config_.value("value", 42));
    }
    
    static CapabilityPhase phase() { return CapabilityPhase::Pre; }

private:
    json config_;
};

class PostCapability {
public:
    PostCapability(const json& config = {}) : config_(config) {}
    
    void init(const json& config) { config_ = config; }
    
    void apply(Context& ctx) {
        ctx.set("post_applied", true);
        ctx.set("post_value", config_.value("value", 99));
    }
    
    static CapabilityPhase phase() { return CapabilityPhase::Post; }

private:
    json config_;
};

YSPEECH_REGISTER_CAPABILITY(PreCapability);
YSPEECH_REGISTER_CAPABILITY(PostCapability);

TEST(CapabilityTest, ConceptSatisfied) {
    static_assert(Capability<PreCapability>);
    static_assert(Capability<PostCapability>);
}

TEST(CapabilityTest, AutoRegisteredNames) {
    EXPECT_TRUE(CapabilityFactory::get_instance().has_capability("PreCapability"));
    EXPECT_TRUE(CapabilityFactory::get_instance().has_capability("PostCapability"));
}

TEST(CapabilityTest, PreCapabilityApply) {
    Context ctx;
    PreCapability cap(json{{"value", 100}});
    cap.init(json{{"value", 100}});
    cap.apply(ctx);
    
    EXPECT_TRUE(ctx.contains("pre_applied"));
    EXPECT_EQ(ctx.get<int>("pre_value"), 100);
}

TEST(CapabilityTest, PostCapabilityApply) {
    Context ctx;
    PostCapability cap(json{{"value", 200}});
    cap.init(json{{"value", 200}});
    cap.apply(ctx);
    
    EXPECT_TRUE(ctx.contains("post_applied"));
    EXPECT_EQ(ctx.get<int>("post_value"), 200);
}

TEST(CapabilityTest, CapabilityIfaceWrapper) {
    Context ctx;
    CapabilityIface iface = CapabilityIface(PreCapability(json{{"value", 50}}), "test_cap");
    
    EXPECT_EQ(iface.name(), "test_cap");
    EXPECT_EQ(iface.phase(), CapabilityPhase::Pre);
    
    iface.init(json{{"value", 50}});
    iface.apply(ctx);
    
    EXPECT_TRUE(ctx.contains("pre_applied"));
}

TEST(CapabilityTest, CapabilityFactory) {
    auto cap = CapabilityFactory::get_instance().create_capability("PreCapability", json{{"value", 77}});
    
    Context ctx;
    cap.apply(ctx);
    
    EXPECT_TRUE(ctx.contains("pre_applied"));
    EXPECT_EQ(ctx.get<int>("pre_value"), 77);
}

TEST(OperatorCapabilityTest, InstallUninstall) {
    OperatorIface op = OperatorIface(StreamTestOp{});
    
    EXPECT_EQ(op.capability_count(), 0);
    
    op.install(PreCapability{}, "pre_cap_test");
    EXPECT_EQ(op.capability_count(), 1);
    EXPECT_TRUE(op.has_capability("pre_cap_test"));
    
    op.install(PostCapability{}, "post_cap_test");
    EXPECT_EQ(op.capability_count(), 2);
    
    auto caps = op.list_capabilities();
    EXPECT_EQ(caps.size(), 2);
    
    EXPECT_TRUE(op.uninstall("pre_cap_test"));
    EXPECT_EQ(op.capability_count(), 1);
    EXPECT_FALSE(op.has_capability("pre_cap_test"));
    
    op.uninstall_all();
    EXPECT_EQ(op.capability_count(), 0);
}

TEST(OperatorCapabilityTest, PrePostPhases) {
    OperatorIface op = OperatorIface(StreamTestOp{});
    op.install(PreCapability(json{{"value", 10}}), "pre");
    op.install(PostCapability(json{{"value", 20}}), "post");
    
    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 8);
    op.process_stream(ctx, store);
    
    EXPECT_TRUE(ctx.contains("pre_applied"));
    EXPECT_TRUE(ctx.contains("op_processed"));
    EXPECT_TRUE(ctx.contains("post_applied"));
    
    EXPECT_EQ(ctx.get<int>("pre_value"), 10);
    EXPECT_EQ(ctx.get<int>("post_value"), 20);
}

TEST(OperatorCapabilityTest, ConfigInstall) {
    OperatorIface op = OperatorIface(StreamTestOp{});
    
    json config = {
        {"capabilities", {
            {{"name", "PreCapability"}, {"params", {{"value", 123}}}}
        }}
    };
    
    op.init(config);
    
    EXPECT_EQ(op.capability_count(), 1);
    EXPECT_TRUE(op.has_capability("PreCapability"));
    
    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 8);
    op.process_stream(ctx, store);
    
    EXPECT_EQ(ctx.get<int>("pre_value"), 123);
}
