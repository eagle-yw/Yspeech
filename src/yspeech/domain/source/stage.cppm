module;

#include <nlohmann/json.hpp>

export module yspeech.domain.source.stage;

import std;
import yspeech.domain.source.base;
import yspeech.domain.source.builtin;
import yspeech.runtime.runtime_context;
import yspeech.runtime.segment_registry;
import yspeech.runtime.stage_adapter;
import yspeech.runtime.token;
import yspeech.types;

namespace yspeech {

export class SourceStage {
public:
    void init(const nlohmann::json& config) {
        core_name_ = config.value("core_name", std::string("PassThroughSource"));
        adapter_.init(config, config.value("__core_id", core_id_));
        core_ = SourceCoreFactory::get_instance().create_core(core_name_);
        core_->init(adapter_.config());
    }

    void process(PipelineToken& token, RuntimeContext& runtime, SegmentRegistry&) {
        if (!core_) {
            return;
        }

        adapter_.run(runtime, [&]() {
            core_->process(token, runtime);
        });
    }

    void deinit() {
        if (core_) {
            core_->deinit();
        }
        core_.reset();
        adapter_.reset();
    }

private:
    std::string core_name_ = "PassThroughSource";
    std::string core_id_ = "source";
    std::unique_ptr<SourceCoreIface> core_;
    StageAdapter adapter_;
};

}
