import std;
import yspeech.types;
import yspeech.streaming_asr;
import yspeech.audio.file;

int main(int argc, char* argv[]) {
    std::print("=== Yspeech 流式 ASR 实际音频测试 ===\n\n");
    
    std::string config_file = "configs/streaming_asr.json";
    std::string audio_file = "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav";
    
    if (argc > 1) {
        config_file = argv[1];
    }
    if (argc > 2) {
        audio_file = argv[2];
    }
    
    std::print("配置文件: {}\n", config_file);
    std::print("音频文件: {}\n\n", audio_file);
    
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
                
                const std::int16_t* pcm = reinterpret_cast<const std::int16_t*>(buffer.data());
                std::size_t num_samples = bytes_read / sizeof(std::int16_t);
                
                for (std::size_t i = 0; i < num_samples; ++i) {
                    audio_data.push_back(static_cast<float>(pcm[i]) / 32768.0f);
                }
            }
            
            std::print("加载音频: {} 样本, {} 通道, {}Hz\n\n", 
                audio_data.size(), num_channels, sample_rate);
        }
        
        yspeech::StreamingAsr asr(config_file);
        
        std::atomic<int> result_count{0};
        
        asr.on_result([&result_count](const yspeech::AsrResult& result) {
            result_count++;
            std::print("\n[识别结果 #{}] {}\n", result_count.load(), result.text);
            std::print("  置信度: {}\n", result.confidence);
            std::print("  语言: {}\n", result.language);
        });
        
        asr.on_vad([](bool is_speech, std::int64_t start_ms, std::int64_t end_ms) {
            if (is_speech) {
                std::print("[VAD] 语音开始: {}ms\n", start_ms);
            } else {
                std::print("[VAD] 语音结束: {}ms - {}ms\n", start_ms, end_ms);
            }
        });
        
        asr.on_status([](const std::string& status) {
            std::print("[状态] {}\n", status);
        });
        
        asr.on_performance([](const yspeech::ProcessingStats& stats) {
            std::print("\n=== 性能统计更新 ===\n");
            std::print("{}\n", stats.to_string());
        });
        
        std::print("开始流式识别...\n");
        asr.start();
        
        int chunk_size = 1600;
        int total_chunks = (audio_data.size() + chunk_size - 1) / chunk_size;
        
        std::print("分块推送音频: {} 块, 每块 {} 样本 ({}ms)\n\n", 
            total_chunks, chunk_size, chunk_size * 1000 / sample_rate);
        
        for (int i = 0; i < total_chunks; ++i) {
            int offset = i * chunk_size;
            int remaining = std::min(chunk_size, static_cast<int>(audio_data.size()) - offset);
            
            if (remaining > 0) {
                asr.push_audio(audio_data.data() + offset, remaining);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::print("\n音频推送完成，等待处理...\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        asr.stop();
        
        auto stats = asr.get_stats();
        std::print("\n{}\n", stats.to_string());
        
    } catch (const std::exception& e) {
        std::println("错误：{}", e.what());
        return 1;
    }
    
    return 0;
}
