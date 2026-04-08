import std;
import yspeech.engine;
import yspeech.log;
import yspeech.types;

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
        yspeech::EngineConfigOptions options;
        options.log_level = "warn";
        if (argc > 2) {
            options.audio_path = audio_file;
        }
        if (argc > 3) {
            options.playback_rate = std::stod(argv[3]);
        }

        yspeech::Engine asr(config_file, options);
        std::print("source.path: {}\n", options.audio_path.value_or("<none>"));
        std::print("source.playback_rate: {}\n\n", options.playback_rate.value_or(1.0));
        
        std::atomic<int> result_count{0};
        std::mutex transcript_mutex;
        std::string latest_partial;
        std::string latest_segment_final;
        std::string final_transcript;
        std::string rendered_transcript;
        std::string last_block_text;
        bool inline_partial_visible = false;

        auto normalize_text = [](std::string text) {
            auto not_space = [](unsigned char ch) {
                return !std::isspace(ch);
            };

            auto begin = std::find_if(text.begin(), text.end(), not_space);
            auto end = std::find_if(text.rbegin(), text.rend(), not_space).base();
            if (begin >= end) {
                return std::string{};
            }
            return std::string(begin, end);
        };

        asr.on_event([&](const yspeech::EngineEvent& event) {
            if (event.kind == yspeech::EngineEventKind::VadStart && event.vad_segment.has_value()) {
                {
                    std::lock_guard lock(transcript_mutex);
                    if (inline_partial_visible) {
                        std::print("\n");
                        inline_partial_visible = false;
                    }
                }
                std::print("[VAD] 语音开始: {}ms\n", event.vad_segment->start_ms);
                return;
            }

            if (event.kind == yspeech::EngineEventKind::VadEnd && event.vad_segment.has_value()) {
                {
                    std::lock_guard lock(transcript_mutex);
                    if (inline_partial_visible) {
                        std::print("\n");
                        inline_partial_visible = false;
                    }
                }
                std::print("[VAD] 语音结束: {}ms - {}ms\n", event.vad_segment->start_ms, event.vad_segment->end_ms);
                return;
            }

            if (!event.asr.has_value()) {
                return;
            }

            auto text = normalize_text(event.asr->text);
            if (text.empty()) {
                return;
            }

            if (event.kind == yspeech::EngineEventKind::ResultPartial) {
                std::lock_guard lock(transcript_mutex);
                if (text == latest_partial || text == latest_segment_final || text == final_transcript) {
                    return;
                }

                latest_partial = text;
                result_count++;
                std::string padded = text;
                if (rendered_transcript.size() > padded.size()) {
                    padded.append(rendered_transcript.size() - padded.size(), ' ');
                }
                rendered_transcript = text;
                inline_partial_visible = true;
                std::print("\r\033[2K[实时转写] {}", padded);
                std::cout.flush();
                return;
            }

            if (event.kind == yspeech::EngineEventKind::ResultSegmentFinal) {
                bool should_print = false;
                {
                    std::lock_guard lock(transcript_mutex);
                    if (inline_partial_visible) {
                        std::print("\n");
                        inline_partial_visible = false;
                    }
                    should_print = text != latest_segment_final;
                    latest_segment_final = text;
                    latest_partial.clear();
                    rendered_transcript = text;
                    if (should_print) {
                        last_block_text = text;
                    }
                }
                if (!should_print) {
                    return;
                }
                if (event.vad_segment.has_value()) {
                    std::print(
                        "\n[段最终 {:>5}ms - {:>5}ms] {}\n",
                        event.vad_segment->start_ms,
                        event.vad_segment->end_ms,
                        text
                    );
                }
                return;
            }

            if (event.kind == yspeech::EngineEventKind::ResultStreamFinal) {
                bool should_print = false;
                {
                    std::lock_guard lock(transcript_mutex);
                    if (inline_partial_visible) {
                        std::print("\n");
                        inline_partial_visible = false;
                    }
                    final_transcript = text;
                    latest_partial.clear();
                    rendered_transcript = text;
                    should_print = text != latest_segment_final && text != last_block_text;
                    if (should_print) {
                        last_block_text = text;
                    }
                }
                if (!should_print) {
                    return;
                }
                if (event.vad_segment.has_value()) {
                    std::print(
                        "\n[流最终 {:>5}ms - {:>5}ms] {}\n",
                        event.vad_segment->start_ms,
                        event.vad_segment->end_ms,
                        text
                    );
                } else {
                    std::print("\n[流最终] {}\n", text);
                }
            }
        });
        
        asr.on_status([&](const std::string& status) {
            {
                std::lock_guard lock(transcript_mutex);
                if (inline_partial_visible) {
                    std::print("\n");
                    inline_partial_visible = false;
                }
            }
            std::print("[状态] {}\n", status);
        });

        std::print("开始流式识别...\n");
        asr.start();
        asr.finish();

        std::print("通过 Engine 内置 source 编排推送 10ms AudioFrame...\n\n");
        while (!asr.input_eof_reached()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        std::print("\n音频推送完成，等待剩余识别结果...\n");

        auto settle_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        auto hard_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        int last_result_count = result_count.load();

        while (std::chrono::steady_clock::now() < hard_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            int current_result_count = result_count.load();
            if (current_result_count != last_result_count) {
                last_result_count = current_result_count;
                settle_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                continue;
            }

            if (std::chrono::steady_clock::now() >= settle_deadline) {
                break;
            }
        }
        
        asr.stop();

        {
            std::lock_guard lock(transcript_mutex);
            std::string summary_text;
            if (!final_transcript.empty()) {
                summary_text = final_transcript;
            } else if (!latest_segment_final.empty()) {
                summary_text = latest_segment_final;
            } else if (!latest_partial.empty()) {
                summary_text = latest_partial;
            }
            if (!summary_text.empty() && summary_text != last_block_text) {
                std::print("\n最终转写：{}\n", summary_text);
            }
        }
        
        auto stats = asr.get_stats();
        std::print("\n{}\n", stats.to_string());
        
    } catch (const std::exception& e) {
        std::println("错误：{}", e.what());
        return 1;
    }
    
    return 0;
}
