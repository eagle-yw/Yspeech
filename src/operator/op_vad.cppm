module;

#include "nlohmann/json.hpp"
#include "onnxruntime_cxx_api.h" 

export module yspeech.Operator.Vad;



import std;

import yspeech.Common.Types;
import yspeech.Context;

namespace yspeech {

struct OpVadContext {
    Ort::Value hn;
    Ort::Value cn;    
};

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
    
    auto load(std::string_view path) -> void {
        // auto config_path = std::format("{}/config.json", path);
        // auto file = std::ifstream(config_path);
        
        // if (!file.is_open()) {
        //     throw std::runtime_error("Failed to open file!");
        // }

        // auto j = nlohmann::json();
        // file >> j;

        // sample_rate_ = 16000;

        // auto model_path = std::format("{0}/{1}", path, j["model"].get<std::string>());

        // // cuda_options_ = std::make_unique<OrtCUDAProviderOptions>();
        // // cuda_options_->device_id = 0;
        // // memory_info_ = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
        // env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SileroVAD");
        // options_ = std::make_unique<Ort::SessionOptions>();      
        // options_->SetIntraOpNumThreads(1); // 设置线程数
        // options_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // // options_->AppendExecutionProvider_CUDA(*cuda_options_);
        
        // session_ = std::make_unique<Ort::Session>(*env_, model_path.data(), *options_);
        
        // auto in_cnt = session_->GetInputCount();
        // input_names_.resize(in_cnt);
        // input_names_ptr_.resize(in_cnt);
        // for(auto i = 0; i < in_cnt; i++){
        //     auto v = session_->GetInputNameAllocated(i, allocator_);
        //     input_names_[i] = v.get();
        //     input_names_ptr_[i] = input_names_[i].c_str();
        //     std::println("{}", input_names_[i]);
        //     auto type_info = session_->GetInputTypeInfo(i);
        //     auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
        //     auto node_dims = tensor_info.GetShape();
        //     std::println("{}", node_dims);
        // }
        
        // auto out_cnt = session_->GetOutputCount();
        // output_names_.resize(out_cnt);
        // output_names_ptr_.resize(out_cnt);
        // for(auto i = 0; i < out_cnt; i++){
        //     auto v = session_->GetOutputNameAllocated(i, allocator_);
        //     output_names_[i] = v.get();
        //     output_names_ptr_[i] = output_names_[i].c_str();
        //     std::println("{}", output_names_[i]);
        // }
    }

    auto process(Context& context) -> void {
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
        auto n = i32(10240);
        auto samples = std::vector<float>(n, 1);        
        
        // auto inputs = std::vector<Ort::Value>();
        
        //input sr h c
        auto input_shape = std::vector<int64_t>{1, n};
        auto input = Ort::Value::CreateTensor(memory_info, samples.data(), n, input_shape.data(), input_shape.size());
    
        auto sr_shape = std::vector<int64_t>{1};
        auto sr = Ort::Value::CreateTensor(memory_info, &sample_rate_, 1, sr_shape.data(), sr_shape.size());

        auto h_shape = std::vector<int64_t>{2, 1, 64};
        auto h = Ort::Value::CreateTensor<f32>(allocator_, h_shape.data(), h_shape.size());

        auto c_shape = std::vector<int64_t>{2, 1, 64};
        auto c = Ort::Value::CreateTensor<f32>(allocator_, c_shape.data(), c_shape.size());

        auto inputs = std::array<Ort::Value, 4>{std::move(input), std::move(sr), std::move(h), std::move(c)};
        auto out = session_->Run({}, input_names_ptr_.data(), inputs.data(), inputs.size(), output_names_ptr_.data(), output_names_ptr_.size());

        auto prob = out[0].GetTensorData<float>()[0];
        std::println("prob: {}", prob);
    }
    
    auto register_info(Context& context) -> void {

    }

private:
    void fill(Ort::Value& tensor, auto value){
        auto n = tensor.GetTypeInfo().GetTensorTypeAndShapeInfo().GetElementCount();
        auto p = tensor.GetTensorMutableData<decltype(value)>();
        std::fill(p, p + n, value);
    }   

    Ort::AllocatorWithDefaultOptions allocator_;    
 
    std::unique_ptr<Ort::Env> env_; 
    std::unique_ptr<Ort::SessionOptions> options_;
    std::unique_ptr<OrtCUDAProviderOptions> cuda_options_;   
    std::unique_ptr<Ort::Session> session_;

    std::vector<std::string> input_names_;
    std::vector<const char *> input_names_ptr_;

    std::vector<std::string> output_names_;
    std::vector<const char *> output_names_ptr_;

    std::array<int64_t, 2> x_shape_;
    std::array<int64_t, 3> s_shape_;

    //
    int64_t sample_rate_;
};

}