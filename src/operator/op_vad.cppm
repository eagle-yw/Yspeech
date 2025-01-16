module;

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
        
    }
private:
    Ort::Env env_;    
};

}