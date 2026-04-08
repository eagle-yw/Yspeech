import std;
import yspeech.types;
import yspeech.engine;

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
        yspeech::EngineConfigOptions options;
        options.audio_path = audio_file;
        options.playback_rate = 0.0;
        yspeech::Engine engine(config_file, options);
        std::mutex status_mutex;
        std::condition_variable status_cv;
        bool input_eof_seen = false;
        bool stream_drained_seen = false;

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
        engine.on_status([&](const std::string& status) {
            if (status == "input_eof") {
                {
                    std::lock_guard lock(status_mutex);
                    input_eof_seen = true;
                }
                status_cv.notify_all();
                return;
            }
            if (status == "stream_drained") {
                {
                    std::lock_guard lock(status_mutex);
                    stream_drained_seen = true;
                }
                status_cv.notify_all();
            }
        });

        engine.start();
        engine.finish();
        {
            std::unique_lock lock(status_mutex);
            const bool drained = status_cv.wait_for(lock, std::chrono::seconds(30), [&]() {
                return stream_drained_seen;
            });
            if (!drained) {
                status_cv.wait_for(lock, std::chrono::seconds(5), [&]() {
                    return input_eof_seen || engine.input_eof_reached();
                });
            }
        }
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
