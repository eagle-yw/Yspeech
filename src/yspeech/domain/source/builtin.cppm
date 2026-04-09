module;

#include <nlohmann/json.hpp>

export module yspeech.domain.source.builtin;

import std;
import yspeech.domain.source.base;
import yspeech.runtime.runtime_context;
import yspeech.runtime.token;

namespace yspeech {

namespace {

class PassThroughSourceCore final : public SourceCoreIface {
public:
    void init(const nlohmann::json&) override {
    }

    void process(PipelineToken&, RuntimeContext&) override {
    }

    void deinit() override {
    }
};

SourceCoreRegistrar<PassThroughSourceCore> pass_through_source_registrar("PassThroughSource");
SourceCoreRegistrar<PassThroughSourceCore> file_source_registrar("FileSource");
SourceCoreRegistrar<PassThroughSourceCore> microphone_source_registrar("MicrophoneSource");
SourceCoreRegistrar<PassThroughSourceCore> stream_source_registrar("StreamSource");

}

}
