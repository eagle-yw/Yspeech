import std;
import yspeech.types;
import yspeech.offline_asr;

int main(int argc, char* argv[]) {
    std::println("=== Yspeech 离线 ASR 示例 ===\n");
    
    if (argc < 3) {
        std::println("用法：{} <配置文件> <音频文件>", argv[0]);
        std::println("示例：{} configs/simple_asr.json audio.wav", argv[0]);
        return 1;
    }
    
    std::string config_file = argv[1];
    std::string audio_file = argv[2];
    
    try {
        auto start = std::chrono::steady_clock::now();
        
        yspeech::OfflineAsr asr(config_file);
        auto result = asr.transcribe(audio_file);
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::println("\n=== 转录结果 ===");
        std::println("文本：{}", result.text);
        std::println("置信度：{:.2f}", result.confidence);
        std::println("语言：{}", result.language);
        std::println("处理时间：{}ms", duration.count());
        
        if (!result.words.empty()) {
            std::println("\n=== 分词信息 ===");
            for (const auto& word : result.words) {
                std::println("  {}: {}ms - {}ms (置信度：{:.2f})",
                           word.word, word.start_time_ms, word.end_time_ms, word.confidence);
            }
        }
        
    } catch (const std::exception& e) {
        std::println("错误：{}", e.what());
        return 1;
    }
    
    return 0;
}
