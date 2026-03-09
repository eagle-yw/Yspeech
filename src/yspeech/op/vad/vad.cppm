module;

#include <nlohmann/json.hpp>
#include <cstdint>

export module yspeech.op.vad;

import std;
import yspeech.context;
import yspeech.op;
import yspeech.log;

namespace yspeech {

export class OpVad {
public:
    OpVad() = default;

    OpVad(const OpVad&) = delete;
    OpVad& operator=(const OpVad&) = delete;
    OpVad(OpVad&&) noexcept = default;
    OpVad& operator=(OpVad&&) noexcept = default;

    void init(const nlohmann::json& config) {
        if (config.contains("sample_rate")) {
            sample_rate_ = config["sample_rate"].get<int>();
        }
    }

    void process(Context& ctx) {
    }

    void deinit() {
    }

private:
    int sample_rate_ = 16000;
};

namespace {

OperatorRegistrar<OpVad> registrar("Vad");

}

}
