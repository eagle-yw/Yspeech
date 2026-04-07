import std;
import yspeech.types;
import yspeech.engine;
import yspeech.frame_source;

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
        yspeech::Engine engine(config_file);
        auto file_source = std::make_shared<yspeech::FileSource>(audio_file, "offline", 1.0, false);
        auto pipeline_source = std::make_shared<yspeech::AudioFramePipelineSource>(file_source);
        engine.set_frame_source(pipeline_source);

        yspeech::AsrResult result;
        engine.on_event([&](const yspeech::EngineEvent& event) {
            if (!event.asr.has_value()) {
                return;
            }
            if (event.kind != yspeech::EngineEventKind::ResultSegmentFinal &&
                event.kind != yspeech::EngineEventKind::ResultStreamFinal) {
                return;
            }
            result = *event.asr;
        });

        engine.start();
        while (!engine.input_eof_reached()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        engine.stop();
        
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
