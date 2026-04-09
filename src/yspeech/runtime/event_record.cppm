module;

export module yspeech.runtime.event_record;

import std;
import yspeech.types;
import yspeech.runtime.token;

namespace yspeech {

export enum class RuntimeEventKind {
    Engine,
    Status,
    Alert
};

export struct RuntimeEventRecord {
    RuntimeEventKind kind = RuntimeEventKind::Engine;
    PipelineTokenId token_id = 0;
    std::optional<SegmentId> segment_id;
    std::optional<EngineEvent> event;
    std::string status;
    std::string alert_id;
    std::string alert_message;
};

}
