module;

#include <nlohmann/json.hpp>

export module yspeech.speech_processor;

export import yspeech.types;
export import yspeech.offline_asr;
export import yspeech.streaming_asr;

namespace yspeech {

export using SpeechProcessor [[deprecated("Use OfflineAsr or StreamingAsr instead")]] = OfflineAsr;

export [[deprecated("Use create_offline_asr instead")]] 
SpeechProcessor create_processor(const std::string& config_path) {
    return SpeechProcessor(config_path);
}

export [[deprecated("Use create_offline_asr instead")]] 
SpeechProcessor create_processor(const nlohmann::json& config) {
    return SpeechProcessor(config);
}

} // namespace yspeech
