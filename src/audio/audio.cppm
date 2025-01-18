
export module yspeech.Audio;

import std;
import yspeech.Common.Types;

namespace yspeech {

export enum class SampleRate {
    SR_8000  = 8000,    // 8 kHz
    SR_16000 = 16000,  // 16 kHz
    SR_32000 = 32000,  // 32 kHz
    SR_48000 = 48000,  // 48 kHz
};

// 获取采样率的名称
export std::string_view getSampleRateName(SampleRate rate) {
    static const std::unordered_map<SampleRate, std::string> rateNames = {
        {SampleRate::SR_8000, "8 kHz"},
        {SampleRate::SR_16000, "16 kHz"},
        {SampleRate::SR_32000, "32 kHz"},
        {SampleRate::SR_48000, "48 kHz"},
    };

    auto it = rateNames.find(rate);
    if (it != rateNames.end()) {
        return it->second;
    }
    return "Unknown Sample Rate";
}


template <typename T>
concept AudioStream = requires(T t, Byte* buffer, Size size) {
    { t.read(buffer, size) } -> std::same_as<Size>;
    { t.sampleRate() } -> std::same_as<SampleRate>;
    { t.micNum() } -> std::same_as<int>;
    { t.refNum() } -> std::same_as<int>;

} && NonCopyable<T> && Movable<T>;


export class AudioStreamIface {
public:
    template<AudioStream T>
    AudioStreamIface(T&& stream): self_(std::make_unique<Model<T>>(std::forward<T>(stream))) {}
    
    AudioStreamIface(const AudioStreamIface&) = delete;
    AudioStreamIface& operator=(const AudioStreamIface&) = delete;
    AudioStreamIface(AudioStreamIface&&) noexcept = default;
    AudioStreamIface& operator=(AudioStreamIface&&) noexcept = default;

    auto read(Byte* buff, Size size) -> Size {
        return self_->read(buff, size);
    }

    auto micNum() -> int {
        return self_->micNum();
    }

    auto refNum() -> int {
        return self_->refNum();
    }
    auto sampleRate() -> SampleRate {
        return self_->sampleRate();
    }

private:
    struct Concept {
        virtual ~Concept() = default;
        virtual auto read(Byte* buff, Size size) -> Size = 0;        
        virtual auto micNum() -> int = 0;
        virtual auto refNum() -> int = 0;
        virtual auto sampleRate() -> SampleRate = 0;
        virtual auto type() const -> const std::type_info& = 0;
    };

    template<AudioStream T>
    struct Model: Concept {
        Model(T&& stream): stream_(std::move(stream)) {}
        auto read(Byte* buff, Size size) -> Size override {
            return stream_.read(buff, size);
        }

        auto micNum() -> int override {
            return stream_.micNum();
        }

        auto refNum() -> int override {
            return stream_.refNum();
        }

        auto sampleRate() -> SampleRate override {
            return stream_.sampleRate();
        }

        auto type() const -> const std::type_info& override {
            return typeid(T);
        }

        auto get() -> T& { return stream_; }
        
        T stream_;
    };

    std::unique_ptr<Concept> self_;
};

} // end namespace yspeech