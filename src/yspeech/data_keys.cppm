module;

export module yspeech.data_keys;

import std;
import yspeech.types;

namespace yspeech {

export struct DataKeyInfo {
    std::string key;
    std::string type_name;
    std::string description;
    bool is_buffer = false;
    bool accumulates = false;
    std::string owner_operator;
};

export class DataKeys {
public:
    static constexpr std::string_view KEY_SEPARATOR = "_";
    
    static constexpr std::string_view SUFFIX_DATA = "_data";
    static constexpr std::string_view SUFFIX_METADATA = "_metadata";
    static constexpr std::string_view SUFFIX_RESULTS = "_results";
    static constexpr std::string_view SUFFIX_STATUS = "_status";
    static constexpr std::string_view SUFFIX_FEATURES = "_features";
    static constexpr std::string_view SUFFIX_NUM_FRAMES = "_num_frames";
    static constexpr std::string_view SUFFIX_NUM_BINS = "_num_bins";
    static constexpr std::string_view SUFFIX_TEXT = "_text";
    static constexpr std::string_view SUFFIX_CONFIDENCE = "_confidence";
    static constexpr std::string_view SUFFIX_LANGUAGE = "_language";
    static constexpr std::string_view SUFFIX_EMOTION = "_emotion";
    static constexpr std::string_view SUFFIX_PROBABILITY = "_probability";
    static constexpr std::string_view SUFFIX_IS_SPEECH = "_is_speech";
    static constexpr std::string_view SUFFIX_SEGMENTS = "_segments";
    
    static constexpr std::string_view KEY_AUDIO_EOF = "audio_eof";
    static constexpr std::string_view KEY_AUDIO_SAMPLE_RATE = "audio_sample_rate";
    static constexpr std::string_view KEY_AUDIO_MIC_NUM = "audio_mic_num";
    
    static constexpr std::string_view PREFIX_VAD = "vad";
    static constexpr std::string_view PREFIX_FBANK = "fbank";
    static constexpr std::string_view PREFIX_ASR = "asr";
    
    static std::string make_key(std::string_view prefix, std::string_view suffix) {
        return std::string(prefix) + std::string(suffix);
    }
    
    static bool is_accumulating_key(std::string_view key) {
        return key.ends_with(SUFFIX_RESULTS) ||
               key.ends_with(SUFFIX_SEGMENTS);
    }
    
    static bool is_buffer_key(std::string_view key) {
        return false;
    }
    
    static std::string key_type_name(std::string_view key) {
        if (key.ends_with(SUFFIX_FEATURES)) {
            return "std::vector<std::vector<float>>";
        }
        if (key.ends_with(SUFFIX_RESULTS)) {
            return "std::vector<AsrResult>";
        }
        if (key.ends_with(SUFFIX_SEGMENTS)) {
            return "std::vector<VadSegment>";
        }
        if (key.ends_with(SUFFIX_TEXT)) {
            return "std::string";
        }
        if (key.ends_with(SUFFIX_CONFIDENCE) || key.ends_with(SUFFIX_PROBABILITY)) {
            return "float";
        }
        if (key.ends_with(SUFFIX_IS_SPEECH)) {
            return "bool";
        }
        if (key.ends_with(SUFFIX_NUM_FRAMES) || key.ends_with(SUFFIX_NUM_BINS)) {
            return "int";
        }
        if (key.ends_with(SUFFIX_LANGUAGE) || key.ends_with(SUFFIX_EMOTION)) {
            return "std::string";
        }
        if (key == KEY_AUDIO_EOF) {
            return "bool";
        }
        if (key == KEY_AUDIO_SAMPLE_RATE || key == KEY_AUDIO_MIC_NUM) {
            return "int";
        }
        return "unknown";
    }
    
    static const std::vector<DataKeyInfo>& registered_keys() {
        static const std::vector<DataKeyInfo> keys = {
            {std::string(KEY_AUDIO_EOF), "bool", "Audio end-of-stream flag", false, false, "AudioConverter"},
            {std::string(KEY_AUDIO_SAMPLE_RATE), "int", "Audio sample rate", false, false, "External"},
            {std::string(KEY_AUDIO_MIC_NUM), "int", "Number of microphones", false, false, "External"},
            {make_key(PREFIX_VAD, SUFFIX_PROBABILITY), "float", "VAD probability", false, false, "SileroVad"},
            {make_key(PREFIX_VAD, SUFFIX_IS_SPEECH), "bool", "Is speech flag", false, false, "SileroVad"},
            {make_key(PREFIX_VAD, SUFFIX_SEGMENTS), "std::vector<VadSegment>", "VAD segments", false, true, "SileroVad"},
            {make_key(PREFIX_FBANK, SUFFIX_FEATURES), "std::vector<std::vector<float>>", "Fbank features", false, false, "KaldiFbank"},
            {make_key(PREFIX_FBANK, SUFFIX_NUM_FRAMES), "int", "Number of feature frames", false, false, "KaldiFbank"},
            {make_key(PREFIX_FBANK, SUFFIX_NUM_BINS), "int", "Number of feature bins", false, false, "KaldiFbank"},
            {make_key(PREFIX_ASR, SUFFIX_TEXT), "std::string", "ASR text result", false, false, "ASR"},
            {make_key(PREFIX_ASR, SUFFIX_CONFIDENCE), "float", "ASR confidence", false, false, "ASR"},
            {make_key(PREFIX_ASR, SUFFIX_LANGUAGE), "std::string", "ASR language", false, false, "ASR"},
            {make_key(PREFIX_ASR, SUFFIX_EMOTION), "std::string", "ASR emotion", false, false, "SenseVoice"},
            {make_key(PREFIX_ASR, SUFFIX_RESULTS), "std::vector<AsrResult>", "ASR results", false, true, "ASR"},
        };
        return keys;
    }
    
    static std::vector<DataKeyInfo> get_registered_keys() {
        return registered_keys();
    }
};

export template<typename T>
class TypedKey {
public:
    explicit TypedKey(std::string name, T default_value = T{})
        : name_(std::move(name))
        , default_value_(std::move(default_value)) {}
    
    const std::string& name() const { return name_; }
    const T& default_value() const { return default_value_; }
    
    bool operator==(const TypedKey& other) const { return name_ == other.name_; }
    bool operator!=(const TypedKey& other) const { return name_ != other.name_; }
    
private:
    std::string name_;
    T default_value_;
};

export namespace keys {

    inline const TypedKey<bool> audio_eof{"audio_eof", false};
    inline const TypedKey<int> audio_sample_rate{"audio_sample_rate", 16000};
    inline const TypedKey<int> audio_mic_num{"audio_mic_num", 1};
    
    namespace vad {
        inline const TypedKey<float> probability{"vad_probability", 0.0f};
        inline const TypedKey<bool> is_speech{"vad_is_speech", false};
        inline const TypedKey<std::vector<VadSegment>> segments{"vad_segments", {}};
    }
    
    namespace fbank {
        inline const TypedKey<std::vector<std::vector<float>>> features{"fbank_features", {}};
        inline const TypedKey<int> num_frames{"fbank_num_frames", 0};
        inline const TypedKey<int> num_bins{"fbank_num_bins", 80};
    }
    
    namespace asr {
        inline const TypedKey<std::string> text{"asr_text", ""};
        inline const TypedKey<float> confidence{"asr_confidence", 0.0f};
        inline const TypedKey<std::string> language{"asr_language", "unknown"};
        inline const TypedKey<std::string> emotion{"asr_emotion", ""};
        inline const TypedKey<std::vector<AsrResult>> results{"asr_results", {}};
    }

}

}
