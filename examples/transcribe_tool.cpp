import std;
import yspeech.types;
import yspeech.offline_asr;

void show_help() {
    std::println("用法：transcribe <配置文件> <音频文件> [选项]");
    std::println("");
    std::println("选项:");
    std::println("  --verbose    显示详细信息");
    std::println("  --help       显示帮助");
    std::println("");
    std::println("示例:");
    std::println("  transcribe examples/configs/simple_asr.json audio.wav");
    std::println("  transcribe examples/configs/two_level_asr.json recording.wav --verbose");
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        show_help();
        return 1;
    }
    
    std::string config_file = argv[1];
    std::string audio_file = argv[2];
    
    bool verbose = false;
    
    for (std::size_t i = 3; i < static_cast<std::size_t>(argc); ++i) {
        std::string arg = argv[i];
        if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--help") {
            show_help();
            return 0;
        }
    }
    
    try {
        auto start = std::chrono::steady_clock::now();
        
        yspeech::OfflineAsr asr(config_file);
        auto results = asr.transcribe_file(audio_file);
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::println("=== 转录完成 ===");
        std::println("音频文件：{}", audio_file);
        std::println("配置文件：{}", config_file);
        std::println("处理时间：{}ms", duration.count());
        std::println("结果数量：{}\n", results.size());
        
        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto& result = results[i];
            std::println("[结果 {}]", i + 1);
            std::println("  文本：{}", result.text);
            std::println("  置信度：{:.2f}", result.confidence);
            std::println("  时间：{}ms - {}ms", result.start_time_ms, result.end_time_ms);
            std::println("  语言：{}", result.language);
            
            if (verbose && !result.words.empty()) {
                std::println("  分词详情:");
                for (const auto& word : result.words) {
                    std::println("    {}: {}ms - {}ms (置信度：{:.2f})",
                               word.word, word.start_time_ms, word.end_time_ms, word.confidence);
                }
            }
            std::println("");
        }
        
    } catch (const std::exception& e) {
        std::println("错误：{}", e.what());
        return 1;
    }
    
    return 0;
}
