#include "gtest/gtest.h"
#include <thread>
#include <nlohmann/json.hpp>
import yspeech;

namespace {

} // namespace

TEST(ErrorTest, ErrorCodeConversion) {
    EXPECT_EQ(yspeech::error_code_to_string(yspeech::ErrorCode::Success), "Success");
    EXPECT_EQ(yspeech::error_code_to_string(yspeech::ErrorCode::Unknown), "Unknown");
    EXPECT_EQ(yspeech::error_code_to_string(yspeech::ErrorCode::InvalidConfig), "InvalidConfig");
    EXPECT_EQ(yspeech::error_code_to_string(yspeech::ErrorCode::CoreProcessFailed), "CoreProcessFailed");
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
    
    ctx.record_error("core1", "Test error", "Core", 
                     yspeech::ErrorCode::CoreProcessFailed, 
                     yspeech::ErrorLevel::Error, 1, false);
    
    EXPECT_TRUE(ctx.has_errors());
    EXPECT_EQ(ctx.error_count(), 1);
    
    auto errors = ctx.errors();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0].source, "core1");
    EXPECT_EQ(errors[0].message, "Test error");
    EXPECT_EQ(errors[0].component, "Core");
    EXPECT_EQ(errors[0].code, yspeech::ErrorCode::CoreProcessFailed);
    EXPECT_EQ(errors[0].level, yspeech::ErrorLevel::Error);
    EXPECT_EQ(errors[0].attempt, 1);
    EXPECT_EQ(errors[0].recovered, false);
}

TEST(ErrorTest, ErrorFiltering) {
    yspeech::Context ctx;
    
    ctx.record_error("core1", "Error 1", "Core", 
                     yspeech::ErrorCode::CoreProcessFailed, 
                     yspeech::ErrorLevel::Error);
    ctx.record_error("core1", "Error 2", "Core", 
                     yspeech::ErrorCode::CoreInitFailed, 
                     yspeech::ErrorLevel::Warning);
    ctx.record_error("config", "Config error", "Config", 
                     yspeech::ErrorCode::InvalidConfig, 
                     yspeech::ErrorLevel::Fatal);
    
    auto by_source = ctx.errors_by_source("core1");
    EXPECT_EQ(by_source.size(), 2);
    
    auto by_component = ctx.errors_by_component("Config");
    EXPECT_EQ(by_component.size(), 1);
    
    auto by_level = ctx.errors_by_level(yspeech::ErrorLevel::Fatal);
    EXPECT_EQ(by_level.size(), 1);
    
    auto by_level_error = ctx.errors_by_level(yspeech::ErrorLevel::Error);
    EXPECT_EQ(by_level_error.size(), 1);
}

TEST(ErrorTest, ContextStatisticsStayInternal) {
    yspeech::Context ctx;

    EXPECT_EQ(ctx.error_count(), 0);
    EXPECT_EQ(ctx.recovered_count(), 0);

    ctx.record_error("core1", "Error 1", "Core",
                     yspeech::ErrorCode::CoreProcessFailed,
                     yspeech::ErrorLevel::Error, 1, false);
    ctx.record_error("core1", "Recovered error", "Core",
                     yspeech::ErrorCode::CoreProcessFailed,
                     yspeech::ErrorLevel::Warning, 2, true);

    EXPECT_EQ(ctx.error_count(), 2);
    EXPECT_EQ(ctx.recovered_count(), 1);

    ctx.clear_errors();
    EXPECT_EQ(ctx.error_count(), 0);
    EXPECT_EQ(ctx.recovered_count(), 0);
}

TEST(ErrorTest, ErrorSerialization) {
    nlohmann::json metadata = {
        {"key", "value"},
        {"nested", {{"a", 1}, {"b", 2}}},
        {"array", {1, 2, 3}}
    };
    
    yspeech::Error err{
        .source = "core1",
        .component = "Core",
        .message = "Test error",
        .code = yspeech::ErrorCode::CoreProcessFailed,
        .level = yspeech::ErrorLevel::Error,
        .attempt = 2,
        .recovered = false,
        .timestamp = std::chrono::system_clock::now(),
        .metadata = metadata
    };
    
    std::string str = err.to_string();
    EXPECT_FALSE(str.empty());
    EXPECT_TRUE(str.find("Core") != std::string::npos);
    EXPECT_TRUE(str.find("core1") != std::string::npos);
    EXPECT_TRUE(str.find("Test error") != std::string::npos);
    EXPECT_TRUE(str.find("CoreProcessFailed") != std::string::npos);
    EXPECT_TRUE(str.find("attempt 2") != std::string::npos);
    
    nlohmann::json json = err.to_json();
    EXPECT_EQ(json["source"], "core1");
    EXPECT_EQ(json["component"], "Core");
    EXPECT_EQ(json["message"], "Test error");
    EXPECT_EQ(json["code"], "CoreProcessFailed");
    EXPECT_EQ(json["level"], "Error");
    EXPECT_EQ(json["metadata"]["key"], "value");
    EXPECT_EQ(json["metadata"]["nested"]["a"], 1);
    EXPECT_EQ(json["metadata"]["array"][0], 1);
}

TEST(ErrorTest, SerializationJsonValidity) {
    yspeech::Error err{
        .source = "core1",
        .component = "Core",
        .message = "Test error with \"quotes\" and \\backslash\\",
        .code = yspeech::ErrorCode::CoreProcessFailed,
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
    
    EXPECT_EQ(parsed["source"], "core1");
    EXPECT_EQ(parsed["message"], "Test error with \"quotes\" and \\backslash\\");
}

TEST(ErrorTest, ContextSerialization) {
    yspeech::Context ctx;
    
    ctx.record_error("core1", "Error 1", "Core");
    ctx.record_error("core2", "Error 2", "Core");
    
    nlohmann::json json = ctx.errors_to_json();
    EXPECT_TRUE(json.is_array());
    EXPECT_EQ(json.size(), 2);
    EXPECT_EQ(json[0]["source"], "core1");
    EXPECT_EQ(json[1]["source"], "core2");
    
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
    
    ctx.record_error("core1", "Test error", "Core");
    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(last_source, "core1");
    
    ctx.record_error("core2", "Another error", "Core");
    EXPECT_EQ(callback_count, 2);
    EXPECT_EQ(last_source, "core2");
}

TEST(ErrorTest, ClearErrors) {
    yspeech::Context ctx;
    
    ctx.record_error("core1", "Error 1", "Core");
    ctx.record_error("core2", "Error 2", "Core");
    
    EXPECT_TRUE(ctx.has_errors());
    EXPECT_EQ(ctx.error_count(), 2);
    
    ctx.clear_errors();
    
    EXPECT_FALSE(ctx.has_errors());
    EXPECT_EQ(ctx.error_count(), 0);
    EXPECT_EQ(ctx.errors().size(), 0);
}

TEST(ErrorTest, HasFatalErrors) {
    yspeech::Context ctx;
    
    ctx.record_error("core1", "Error", "Core", 
                     yspeech::ErrorCode::CoreProcessFailed, 
                     yspeech::ErrorLevel::Error);
    
    EXPECT_FALSE(ctx.has_fatal_errors());
    
    ctx.record_error("core2", "Fatal error", "Core", 
                     yspeech::ErrorCode::InvalidConfig, 
                     yspeech::ErrorLevel::Fatal);
    
    EXPECT_TRUE(ctx.has_fatal_errors());
}

TEST(ErrorTest, RecoveredError) {
    yspeech::Context ctx;
    
    ctx.record_error("core1", "Recovered error", "Core", 
                     yspeech::ErrorCode::CoreProcessFailed, 
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
                ctx.record_error("core" + std::to_string(i), "Error", "Core");
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
    
    ctx.record_error("core1", "Error 1", "Core", 
                     yspeech::ErrorCode::CoreProcessFailed);
    ctx.record_error("core2", "Error 2", "Core", 
                     yspeech::ErrorCode::CoreInitFailed);
    ctx.record_error("config.json", "Parse error", "Config", 
                     yspeech::ErrorCode::ConfigParseError);
    
    auto core_errors = ctx.errors_by_component("Core");
    EXPECT_EQ(core_errors.size(), 2);
    
    auto config_errors = ctx.errors_by_component("Config");
    EXPECT_EQ(config_errors.size(), 1);
    EXPECT_EQ(config_errors[0].source, "config.json");
}

TEST(PipelineIntegration, ConfigErrorHandling) {
    EXPECT_THROW(yspeech::Engine engine(std::string("nonexistent.json")), std::exception);
}

TEST(PipelineIntegration, ErrorRecoveryWithMetadata) {
    nlohmann::json config = {
        {"name", "test_pipeline"},
        {"version", "1.0"},
        {"task", "asr"},
        {"mode", "streaming"},
        {"frame", {
            {"sample_rate", 16000},
            {"channels", 1},
            {"dur_ms", 10}
        }},
        {"stream", {
            {"ring_capacity_frames", 8}
        }},
        {"source", {
            {"type", "stream"}
        }},
        {"pipelines", nlohmann::json::array({
            {
                {"id", "stage1"},
                {"ops", nlohmann::json::array({
                    {
                        {"id", "op1"},
                        {"name", "UnknownOp"}
                    }
                })}
            }
        })}
    };

    EXPECT_NO_THROW({
        auto pipeline_config = yspeech::PipelineConfig::from_json(config);
        auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);
        yspeech::RuntimeContext runtime;
        runtime.config = config;
        yspeech::SegmentRegistry registry;
        yspeech::PipelineExecutor executor;
        executor.configure(builder_config, runtime, registry);
    });
}
