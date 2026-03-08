#include <cstdint>
#include <cstddef>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <iostream>

import yspeech.types;
import yspeech.streaming_asr;
import yspeech.audio.file;

int main(int argc, char* argv[]) {
    std::cout << "=== Yspeech 流式 ASR 实际音频测试 ===\n\n";
    
    std::string config_file = "configs/streaming_asr.json";
    std::string audio_file = "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav";
    
    if (argc > 1) {
        config_file = argv[1];
    }
    if (argc > 2) {
        audio_file = argv[2];
    }
    
    std::cout << "配置文件: " << config_file << "\n";
    std::cout << "音频文件: " << audio_file << "\n\n";
    
    try {
        std::vector<float> audio_data;
        int sample_rate = 16000;
        int num_channels = 1;
        
        {
            yspeech::AudioFileStream audio_stream(audio_file);
            sample_rate = static_cast<int>(audio_stream.sampleRate());
            num_channels = audio_stream.micNum();
            
            std::vector<yspeech::Byte> buffer(4096);
            
            while (true) {
                yspeech::Size bytes_read = audio_stream.read(buffer.data(), buffer.size());
                if (bytes_read == 0) break;
                
                const int16_t* pcm = reinterpret_cast<const int16_t*>(buffer.data());
                size_t num_samples = bytes_read / sizeof(int16_t);
                
                for (size_t i = 0; i < num_samples; ++i) {
                    audio_data.push_back(static_cast<float>(pcm[i]) / 32768.0f);
                }
            }
            
            std::cout << "加载音频: " << audio_data.size() << " 样本, " 
                      << num_channels << " 通道, " << sample_rate << "Hz\n\n";
        }
        
        yspeech::StreamingAsr asr(config_file);
        
        std::atomic<int> result_count{0};
        
        asr.on_result([&result_count](const yspeech::AsrResult& result) {
            result_count++;
            std::cout << "\n[识别结果 #" << result_count.load() << "] " << result.text << "\n";
            std::cout << "  置信度: " << result.confidence << "\n";
            std::cout << "  语言: " << result.language << "\n";
        });
        
        asr.on_vad([](bool is_speech, std::int64_t start_ms, std::int64_t end_ms) {
            if (is_speech) {
                std::cout << "[VAD] 语音开始: " << start_ms << "ms\n";
            } else {
                std::cout << "[VAD] 语音结束: " << start_ms << "ms - " << end_ms << "ms\n";
            }
        });
        
        asr.on_status([](const std::string& status) {
            std::cout << "[状态] " << status << "\n";
        });
        
        std::cout << "开始流式识别...\n";
        asr.start();
        
        int chunk_size = 1600;
        int total_chunks = (audio_data.size() + chunk_size - 1) / chunk_size;
        
        std::cout << "分块推送音频: " << total_chunks << " 块, 每块 " << chunk_size 
                  << " 样本 (" << (chunk_size * 1000 / sample_rate) << "ms)\n\n";
        
        auto start_time = std::chrono::steady_clock::now();
        
        for (int i = 0; i < total_chunks; ++i) {
            int offset = i * chunk_size;
            int remaining = std::min(chunk_size, static_cast<int>(audio_data.size()) - offset);
            
            if (remaining > 0) {
                asr.push_audio(audio_data.data() + offset, remaining);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::cout << "\n音频推送完成，等待处理...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        asr.stop();
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        auto stats = asr.get_stats();
        std::cout << "\n=== 统计信息 ===\n";
        std::cout << "处理时间: " << duration.count() << "ms\n";
        std::cout << "音频块数: " << stats.audio_chunks_processed << "\n";
        std::cout << "语音段数: " << stats.speech_segments_detected << "\n";
        std::cout << "结果数: " << stats.asr_results_generated << "\n";
        
        double audio_duration_ms = static_cast<double>(audio_data.size()) / sample_rate * 1000;
        double rtf = duration.count() / audio_duration_ms;
        std::cout << "音频时长: " << audio_duration_ms << "ms\n";
        std::cout << "RTF: " << rtf << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "错误：" << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
