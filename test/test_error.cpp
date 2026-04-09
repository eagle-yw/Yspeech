#include "gtest/gtest.h"
#include <thread>
#include <nlohmann/json.hpp>
import yspeech;

namespace {

struct TestErrorNoopOp {
    void init(const nlohmann::json&) {
    }

    auto process_stream(yspeech::Context&, yspeech::StreamStore&) -> yspeech::StreamProcessResult {
        return {
            .status = yspeech::StreamProcessStatus::ProducedOutput,
            .wake_downstream = true
        };
    }
};

yspeech::OperatorRegistrar<TestErrorNoopOp> test_error_noop_registrar("TestErrorNoopOp");

} // namespace

TEST(ErrorTest, ErrorCodeConversion) {
    EXPECT_EQ(yspeech::error_code_to_string(yspeech::ErrorCode::Success), "Success");
    EXPECT_EQ(yspeech::error_code_to_string(yspeech::ErrorCode::Unknown), "Unknown");
    EXPECT_EQ(yspeech::error_code_to_string(yspeech::ErrorCode::InvalidConfig), "InvalidConfig");
    EXPECT_EQ(yspeech::error_code_to_string(yspeech::ErrorCode::OperatorProcessFailed), "OperatorProcessFailed");
    EXPECT_EQ(yspeech::error_code_to_string(yspeech::ErrorCode::ResourceNotFound), "ResourceNotFound");
    EXPECT_EQ(yspeech::error_code_to_string(yspeech::ErrorCode::Timeout), "Timeout");
    EXPECT_EQ(yspeech::error_code_to_string(yspeech::ErrorCode::NetworkError), "NetworkError");
}

TEST(ErrorTest, ErrorLevelConversion) {
    EXPECT_EQ(yspeech::error_level_to_string(yspeech::ErrorLevel::Info), "Info");
    EXPECT_EQ(yspeech::error_level_to_string(yspeech::ErrorLevel::Warning), "Warning");
    EXPECT_EQ(yspeech::error_level_to_string(yspeech::ErrorLevel::Error), "Error");
    EXPECT_EQ(yspeech::error_level_to_string(yspeech::ErrorLevel::Fatal), "Fatal");
}

TEST(ErrorTest, ContextErrorRecording) {
    yspeech::Context ctx;
    
    EXPECT_FALSE(ctx.has_errors());
    EXPECT_EQ(ctx.error_count(), 0);
    
    ctx.record_error("op1", "Test error", "Operator", 
                     yspeech::ErrorCode::OperatorProcessFailed, 
                     yspeech::ErrorLevel::Error, 1, false);
    
    EXPECT_TRUE(ctx.has_errors());
    EXPECT_EQ(ctx.error_count(), 1);
    
    auto errors = ctx.errors();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0].source, "op1");
    EXPECT_EQ(errors[0].message, "Test error");
    EXPECT_EQ(errors[0].component, "Operator");
    EXPECT_EQ(errors[0].code, yspeech::ErrorCode::OperatorProcessFailed);
    EXPECT_EQ(errors[0].level, yspeech::ErrorLevel::Error);
    EXPECT_EQ(errors[0].attempt, 1);
    EXPECT_EQ(errors[0].recovered, false);
}

TEST(ErrorTest, ErrorFiltering) {
    yspeech::Context ctx;
    
    ctx.record_error("op1", "Error 1", "Operator", 
                     yspeech::ErrorCode::OperatorProcessFailed, 
                     yspeech::ErrorLevel::Error);
    ctx.record_error("op1", "Error 2", "Operator", 
                     yspeech::ErrorCode::OperatorInitFailed, 
                     yspeech::ErrorLevel::Warning);
    ctx.record_error("config", "Config error", "Config", 
                     yspeech::ErrorCode::InvalidConfig, 
                     yspeech::ErrorLevel::Fatal);
    
    auto by_source = ctx.errors_by_source("op1");
    EXPECT_EQ(by_source.size(), 2);
    
    auto by_component = ctx.errors_by_component("Config");
    EXPECT_EQ(by_component.size(), 1);
    
    auto by_level = ctx.errors_by_level(yspeech::ErrorLevel::Fatal);
    EXPECT_EQ(by_level.size(), 1);
    
    auto by_level_error = ctx.errors_by_level(yspeech::ErrorLevel::Error);
    EXPECT_EQ(by_level_error.size(), 1);
}

TEST(ErrorTest, StateStatistics) {
    yspeech::State state;
    
    EXPECT_EQ(state.total_errors.load(), 0);
    EXPECT_EQ(state.recovered_errors.load(), 0);
    EXPECT_EQ(state.skipped_operators.load(), 0);
    
    state.mark_error();
    state.mark_error();
    state.mark_recovered();
    state.mark_skipped();
    
    EXPECT_EQ(state.total_errors.load(), 2);
    EXPECT_EQ(state.recovered_errors.load(), 1);
    EXPECT_EQ(state.skipped_operators.load(), 1);
    
    state.reset();
    EXPECT_EQ(state.total_errors.load(), 0);
    EXPECT_EQ(state.recovered_errors.load(), 0);
    EXPECT_EQ(state.skipped_operators.load(), 0);
}

TEST(ErrorTest, ErrorSerialization) {
    nlohmann::json metadata = {
        {"key", "value"},
        {"nested", {{"a", 1}, {"b", 2}}},
        {"array", {1, 2, 3}}
    };
    
    yspeech::Error err{
        .source = "op1",
        .component = "Operator",
        .message = "Test error",
        .code = yspeech::ErrorCode::OperatorProcessFailed,
        .level = yspeech::ErrorLevel::Error,
        .attempt = 2,
        .recovered = false,
        .timestamp = std::chrono::system_clock::now(),
        .metadata = metadata
    };
    
    std::string str = err.to_string();
    EXPECT_FALSE(str.empty());
    EXPECT_TRUE(str.find("Operator") != std::string::npos);
    EXPECT_TRUE(str.find("op1") != std::string::npos);
    EXPECT_TRUE(str.find("Test error") != std::string::npos);
    EXPECT_TRUE(str.find("OperatorProcessFailed") != std::string::npos);
    EXPECT_TRUE(str.find("attempt 2") != std::string::npos);
    
    nlohmann::json json = err.to_json();
    EXPECT_EQ(json["source"], "op1");
    EXPECT_EQ(json["component"], "Operator");
    EXPECT_EQ(json["message"], "Test error");
    EXPECT_EQ(json["code"], "OperatorProcessFailed");
    EXPECT_EQ(json["level"], "Error");
    EXPECT_EQ(json["metadata"]["key"], "value");
    EXPECT_EQ(json["metadata"]["nested"]["a"], 1);
    EXPECT_EQ(json["metadata"]["array"][0], 1);
}

TEST(ErrorTest, SerializationJsonValidity) {
    yspeech::Error err{
        .source = "op1",
        .component = "Operator",
        .message = "Test error with \"quotes\" and \\backslash\\",
        .code = yspeech::ErrorCode::OperatorProcessFailed,
        .level = yspeech::ErrorLevel::Error,
        .attempt = 2,
        .recovered = false,
        .timestamp = std::chrono::system_clock::now(),
        .metadata = nlohmann::json{{"key", "value with \"quotes\""}}
    };
    
    nlohmann::json json = err.to_json();
    EXPECT_NO_THROW(json.dump());
    
    std::string json_str = json.dump();
    nlohmann::json parsed;
    EXPECT_NO_THROW(parsed = nlohmann::json::parse(json_str));
    
    EXPECT_EQ(parsed["source"], "op1");
    EXPECT_EQ(parsed["message"], "Test error with \"quotes\" and \\backslash\\");
}

TEST(ErrorTest, ContextSerialization) {
    yspeech::Context ctx;
    
    ctx.record_error("op1", "Error 1", "Operator");
    ctx.record_error("op2", "Error 2", "Operator");
    
    nlohmann::json json = ctx.errors_to_json();
    EXPECT_TRUE(json.is_array());
    EXPECT_EQ(json.size(), 2);
    EXPECT_EQ(json[0]["source"], "op1");
    EXPECT_EQ(json[1]["source"], "op2");
    
    std::string summary = ctx.errors_summary();
    EXPECT_TRUE(summary.find("Total: 2") != std::string::npos);
    EXPECT_TRUE(summary.find("Recovered: 0") != std::string::npos);
}

TEST(ErrorTest, ErrorCallback) {
    yspeech::Context ctx;
    int callback_count = 0;
    std::string last_source;
    
    ctx.set_error_callback([&](const yspeech::Error& err) {
        callback_count++;
        last_source = err.source;
    });
    
    ctx.record_error("op1", "Test error", "Operator");
    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(last_source, "op1");
    
    ctx.record_error("op2", "Another error", "Operator");
    EXPECT_EQ(callback_count, 2);
    EXPECT_EQ(last_source, "op2");
}

TEST(ErrorTest, ClearErrors) {
    yspeech::Context ctx;
    
    ctx.record_error("op1", "Error 1", "Operator");
    ctx.record_error("op2", "Error 2", "Operator");
    
    EXPECT_TRUE(ctx.has_errors());
    EXPECT_EQ(ctx.error_count(), 2);
    
    ctx.clear_errors();
    
    EXPECT_FALSE(ctx.has_errors());
    EXPECT_EQ(ctx.error_count(), 0);
    EXPECT_EQ(ctx.errors().size(), 0);
}

TEST(ErrorTest, HasFatalErrors) {
    yspeech::Context ctx;
    
    ctx.record_error("op1", "Error", "Operator", 
                     yspeech::ErrorCode::OperatorProcessFailed, 
                     yspeech::ErrorLevel::Error);
    
    EXPECT_FALSE(ctx.has_fatal_errors());
    
    ctx.record_error("op2", "Fatal error", "Operator", 
                     yspeech::ErrorCode::InvalidConfig, 
                     yspeech::ErrorLevel::Fatal);
    
    EXPECT_TRUE(ctx.has_fatal_errors());
}

TEST(ErrorTest, RecoveredError) {
    yspeech::Context ctx;
    
    ctx.record_error("op1", "Recovered error", "Operator", 
                     yspeech::ErrorCode::OperatorProcessFailed, 
                     yspeech::ErrorLevel::Info, 3, true);
    
    EXPECT_EQ(ctx.recovered_count(), 1);
    
    auto errors = ctx.errors();
    EXPECT_EQ(errors[0].recovered, true);
    EXPECT_EQ(errors[0].attempt, 3);
}

TEST(ErrorTest, MetadataWithJson) {
    yspeech::Context ctx;
    
    nlohmann::json metadata = {
        {"config_path", "/path/to/config.json"},
        {"line_number", 42},
        {"dependency_chain", {"op1", "op2", "op3"}}
    };
    
    ctx.record_error("config", "Config parse error", "Config",
                     yspeech::ErrorCode::ConfigParseError,
                     yspeech::ErrorLevel::Error,
                     0, false, metadata);
    
    auto errors = ctx.errors();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0].metadata["config_path"], "/path/to/config.json");
    EXPECT_EQ(errors[0].metadata["line_number"], 42);
    EXPECT_EQ(errors[0].metadata["dependency_chain"].size(), 3);
    
    nlohmann::json json = errors[0].to_json();
    EXPECT_EQ(json["metadata"]["config_path"], "/path/to/config.json");
}

TEST(ErrorTest, ThreadSafety) {
    yspeech::Context ctx;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&ctx, i]() {
            for (int j = 0; j < 10; ++j) {
                ctx.record_error("op" + std::to_string(i), "Error", "Operator");
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(ctx.error_count(), 100);
}

TEST(ContextTest, ErrorFilteringByComponent) {
    yspeech::Context ctx;
    
    ctx.record_error("op1", "Error 1", "Operator", 
                     yspeech::ErrorCode::OperatorProcessFailed);
    ctx.record_error("op2", "Error 2", "Operator", 
                     yspeech::ErrorCode::OperatorInitFailed);
    ctx.record_error("config.json", "Parse error", "Config", 
                     yspeech::ErrorCode::ConfigParseError);
    
    auto op_errors = ctx.errors_by_component("Operator");
    EXPECT_EQ(op_errors.size(), 2);
    
    auto config_errors = ctx.errors_by_component("Config");
    EXPECT_EQ(config_errors.size(), 1);
    EXPECT_EQ(config_errors[0].source, "config.json");
}

TEST(PipelineIntegration, ConfigErrorHandling) {
    yspeech::PipelineManager pipeline;
    
    EXPECT_THROW(pipeline.build("nonexistent.json"), std::exception);
    EXPECT_FALSE(pipeline.has_build_errors());
    EXPECT_TRUE(pipeline.build_errors().empty());
}

TEST(PipelineIntegration, ErrorRecoveryWithMetadata) {
    yspeech::Context ctx;
    yspeech::PipelineManager pipeline;
    yspeech::StreamStore store;
    
    nlohmann::json config = {
        {"name", "test_pipeline"},
        {"version", "1.0"},
        {"pipelines", nlohmann::json::array({
            {
                {"id", "stage1"},
                {"ops", nlohmann::json::array({
                    {
                        {"id", "op1"},
                        {"name", "TestErrorNoopOp"}
                    }
                })}
            }
        })}
    };
    
    pipeline.build(yspeech::PipelineConfig::from_json(config));
    
    EXPECT_FALSE(pipeline.has_build_errors());
    
    store.init_audio_ring("audio_frames", 8);
    pipeline.run_stream(ctx, store, false);
    
    EXPECT_FALSE(ctx.has_errors());
}
