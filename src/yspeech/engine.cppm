module;

#include <nlohmann/json.hpp>

export module yspeech.engine;

import std;
import yspeech.context;
import yspeech.aspect;
import yspeech.pipeline;
import yspeech.pipeline_config;
import yspeech.op;
import yspeech.log;

namespace yspeech {

export class Engine {
public:
    Engine() = default;
    
    ~Engine() = default;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept = delete;
    Engine& operator=(Engine&&) noexcept = delete;

    void init(const std::string& config_path) {
        log_init("yspeech");
        pipeline_.build(config_path);
        log_info("Engine initialized from: {}", config_path);
    }
    
    void init(const nlohmann::json& config) {
        log_init("yspeech");
        pipeline_.build(PipelineConfig::from_json(config));
        log_info("Engine initialized from config");
    }
    
    void run(Context& ctx) {
        log_info("Engine running");
        pipeline_.run(ctx);
    }
    
    void clear() {
        pipeline_.clear();
    }

private:
    Pipeline pipeline_;
};

}
