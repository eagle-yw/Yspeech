export module yspeech.Audio.AudioFile;

import std;
import yspeech.Common.Types;
import yspeech.Audio;

namespace yspeech {

struct WavHeader {
    char chunk_id[4];          // "RIFF"
    std::uint32_t chunk_size;       // 文件总大小减去 8 字节
    char format[4];            // "WAVE"
    char subchunk1_id[4];      // "fmt"
    std::uint32_t subchunk1_size;   // 格式块大小（通常是 16）
    std::uint16_t audio_format;     // 音频格式（1 表示 PCM）
    std::uint16_t num_channels;      // 通道数
    std::uint32_t sample_rate;       // 采样率
    std::uint32_t byte_rate;         // 每秒字节数
    std::uint16_t block_align;       // 每个样本的字节数
    std::uint16_t bits_per_sample;    // 每个样本的位数
    char subchunk2_id[4];       // "data"
    std::uint32_t subchunk2_size;    // 音频数据大小
};

export class AudioFileStream {
public:
    AudioFileStream() = default;
    
    AudioFileStream(std::string_view file_path){
        file_ = std::make_unique<std::ifstream>(std::string(file_path), std::ios::binary);
        if (!file_->is_open()) {
            auto tmp = std::format("Failed to open file: {}", file_path);
            throw std::runtime_error(tmp);
        }

        if (file_path.find(".wav") != std::string::npos){
            auto header = WavHeader();
            file_->read(reinterpret_cast<char*>(&header), sizeof(WavHeader));
            if(header.audio_format != 1){ //only support pcm
                throw std::runtime_error("Only supported pcm stream");
            }

        } else if (file_path.find(".pcm") != std::string::npos){

        } else {
            auto tmp = std::format("Unsupported file format, only pcm or wav format is supported. file: {}", file_path);
            throw std::runtime_error(tmp);
        }
    }
    
    ~AudioFileStream(){

    }

    AudioFileStream(const AudioFileStream&) = delete;
    AudioFileStream& operator=(const AudioFileStream&) = delete;
    AudioFileStream(AudioFileStream&&) noexcept = default;
    AudioFileStream& operator=(AudioFileStream&&) noexcept = default;

    auto read(Byte* buff, Size size) -> Size {
        file_->read(reinterpret_cast<char*>(buff), size);
        return file_->gcount();
    }

    auto micNum() -> int {
        return mic_num_;
    }

    auto refNum() -> int {
        return ref_num_;
    }
    auto sampleRate() -> SampleRate {
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
    int mic_num_;
    int ref_num_;
    SampleRate sr_;
    std::unique_ptr<std::ifstream> file_;
};

}
