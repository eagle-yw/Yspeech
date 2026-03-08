module;

export module yspeech.audio.file;

import std;
import yspeech.audio;
import yspeech.types;

namespace yspeech {

namespace detail {

struct WavHeader {
    char chunk_id[4];
    std::uint32_t chunk_size;
    char format[4];
    char subchunk1_id[4];
    std::uint32_t subchunk1_size;
    std::uint16_t audio_format;
    std::uint16_t num_channels;
    std::uint32_t sample_rate;
    std::uint32_t byte_rate;
    std::uint16_t block_align;
    std::uint16_t bits_per_sample;
    char subchunk2_id[4];
    std::uint32_t subchunk2_size;
};

inline SampleRate toSampleRate(std::uint32_t rate) {
    switch (rate) {
        case 8000:  return SampleRate::SR_8000;
        case 16000: return SampleRate::SR_16000;
        case 32000: return SampleRate::SR_32000;
        case 48000: return SampleRate::SR_48000;
        default:    return SampleRate::SR_16000;
    }
}

}

export class AudioFileStream {
public:
    AudioFileStream() = default;
    
    AudioFileStream(std::string_view file_path) {
        file_ = std::make_unique<std::ifstream>(std::string(file_path), std::ios::binary);
        if (!file_->is_open()) {
            auto tmp = std::format("Failed to open file: {}", file_path);
            throw std::runtime_error(tmp);
        }

        if (file_path.find(".wav") != std::string::npos) {
            auto header = detail::WavHeader();
            file_->read(reinterpret_cast<char*>(&header), sizeof(detail::WavHeader));
            if (header.audio_format != 1) {
                throw std::runtime_error("Only supported pcm stream");
            }
            sr_ = detail::toSampleRate(header.sample_rate);
            mic_num_ = header.num_channels;
        } else if (file_path.find(".pcm") != std::string::npos) {
            // PCM 文件需要手动设置参数
        } else {
            auto tmp = std::format("Unsupported file format, only pcm or wav format is supported. file: {}", file_path);
            throw std::runtime_error(tmp);
        }
    }
    
    ~AudioFileStream() = default;

    AudioFileStream(const AudioFileStream&) = delete;
    AudioFileStream& operator=(const AudioFileStream&) = delete;
    AudioFileStream(AudioFileStream&&) noexcept = default;
    AudioFileStream& operator=(AudioFileStream&&) noexcept = default;

    auto read(Byte* buff, Size size) -> Size {
        file_->read(reinterpret_cast<char*>(buff), size);
        return file_->gcount();
    }

    auto micNum() const -> int {
        return mic_num_;
    }

    auto refNum() const -> int {
        return ref_num_;
    }

    auto sampleRate() const -> SampleRate {
        return sr_;
    }

    auto micNum(int mic_num) -> void {
        mic_num_ = mic_num;
    }

    auto refNum(int ref_num) -> void {
        ref_num_ = ref_num;
    }
    
    auto sampleRate(SampleRate sr) -> void {
        sr_ = sr;
    }

private:
    int mic_num_ = 1;
    int ref_num_ = 0;
    SampleRate sr_ = SampleRate::SR_16000;
    std::unique_ptr<std::ifstream> file_;
};

}
