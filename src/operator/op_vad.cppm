module;

#include "nlohmann/json.hpp"
#include "onnxruntime_cxx_api.h" 

export module yspeech.Operator.Vad;



import std;

import yspeech.Common.Types;
import yspeech.Context;

namespace yspeech {
    
export class OpVad {
public:
    OpVad() = default;

    // Only movement allowed
    OpVad(const OpVad&) = delete;
    OpVad& operator=(const OpVad&) = delete;
    OpVad(OpVad&&) noexcept = default;
    OpVad& operator=(OpVad&&) noexcept = default;
    
    ~OpVad(){
        std::println("OpVad::~OpVad()");
    };
    
    auto process(Context context) -> void {

    }
    
    auto load(std::string_view path) -> void {
        auto config_path = std::format("{}/config.json", path);
        auto file = std::ifstream(config_path);
        
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file!");
        }

        auto j = nlohmann::json();
        file >> j;

        auto model_path = std::format("{0}/{1}", path, j["model"].get<std::string>());
        
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SileroVAD");
        options_ = std::make_unique<Ort::SessionOptions>();      
        options_->SetIntraOpNumThreads(1); // 设置线程数
        options_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = std::make_unique<Ort::Session>(*env_, model_path.data(), *options_);
    }
private:        
    std::unique_ptr<Ort::Env> env_; 
    std::unique_ptr<Ort::SessionOptions> options_;  
    std::unique_ptr<Ort::Session> session_;
};

}