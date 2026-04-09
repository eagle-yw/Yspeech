module;

#include <nlohmann/json.hpp>

export module yspeech.stream_process;

import std;

namespace yspeech {

export enum class StreamProcessStatus {
    NoOp,
    ConsumedInput,
    ProducedOutput,
    SegmentFinalized,
    StreamFinalized,
    NeedMoreInput,
    OverrunRecovered
};

export struct StreamProcessResult {
    StreamProcessStatus status = StreamProcessStatus::NoOp;
    std::size_t consumed_frames = 0;
    std::size_t produced_items = 0;
    bool wake_downstream = false;
};

}
