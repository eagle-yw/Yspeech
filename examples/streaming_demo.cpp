import std;
import yspeech.engine;
import yspeech.log;
import yspeech.types;

namespace {

struct DemoOptions {
    std::string config_file = "configs/streaming_asr.json";
    std::string audio_file = "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav";
    double playback_rate = 1.0;
    bool enable_event_queue = true;
    int benchmark_runs = 0;
    bool quiet = false;
};

auto parse_bool_flag(const std::string& value) -> bool {
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

void print_help(const char* program) {
    std::println("用法: {} [config] [audio] [playback_rate] [queue_flag] [选项]", program);
    std::println("");
    std::println("位置参数:");
    std::println("  config         配置文件路径");
    std::println("  audio          音频文件路径");
    std::println("  playback_rate  音频推送倍率，1.0 为实时，20 为 20 倍速");
    std::println("  queue_flag     兼容旧参数: 1/0 或 true/false，控制 internal event queue");
    std::println("");
    std::println("选项:");
    std::println("  --queue <bool>        显式设置 internal event queue");
    std::println("  --benchmark <N>       连续运行 N 次并输出统计");
    std::println("  --quiet               减少过程输出（benchmark 默认开启）");
    std::println("  --help                显示帮助");
}

auto parse_args(int argc, char* argv[]) -> DemoOptions {
    DemoOptions opts;
    if (argc > 1) {
        opts.config_file = argv[1];
    }
    if (argc > 2) {
        opts.audio_file = argv[2];
    }
    if (argc > 3) {
        opts.playback_rate = std::stod(argv[3]);
    }

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            print_help(argv[0]);
            std::exit(0);
        }
        if (arg == "--quiet") {
            opts.quiet = true;
            continue;
        }
        if (arg == "--benchmark" && i + 1 < argc) {
            opts.benchmark_runs = std::max(0, std::stoi(argv[++i]));
            continue;
        }
        if (arg == "--queue" && i + 1 < argc) {
            opts.enable_event_queue = parse_bool_flag(argv[++i]);
            continue;
        }

        // Backward compatibility: positional queue flag at argv[4]
        if (i == 4) {
            opts.enable_event_queue = parse_bool_flag(arg);
        }
    }

    if (opts.benchmark_runs > 0) {
        opts.quiet = true;
    }

    return opts;
}

struct RunResult {
    yspeech::ProcessingStats stats;
    std::string final_text;
};

auto run_once(const DemoOptions& opts, bool verbose) -> RunResult {
    yspeech::EngineConfigOptions engine_opts;
    engine_opts.log_level = "warn";
    engine_opts.audio_path = opts.audio_file;
    engine_opts.playback_rate = opts.playback_rate;
    engine_opts.enable_event_queue = opts.enable_event_queue;

    yspeech::Engine asr(opts.config_file, engine_opts);
    std::mutex status_mutex;
    std::condition_variable status_cv;
    bool input_eof_seen = false;
    bool stream_drained_seen = false;

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
        if (!verbose) {
            if (event.asr.has_value() &&
                (event.kind == yspeech::EngineEventKind::ResultSegmentFinal ||
                 event.kind == yspeech::EngineEventKind::ResultStreamFinal)) {
                auto text = normalize_text(event.asr->text);
                if (!text.empty()) {
                    std::lock_guard lock(transcript_mutex);
                    final_transcript = text;
                }
            }
            return;
        }

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
                final_transcript = text;
                if (should_print) {
                    last_block_text = text;
                }
            }
            if (!should_print) {
                return;
            }
            if (event.vad_segment.has_value()) {
                std::print("\n[段最终 {:>5}ms - {:>5}ms] {}\n",
                           event.vad_segment->start_ms,
                           event.vad_segment->end_ms,
                           text);
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
                std::print("\n[流最终 {:>5}ms - {:>5}ms] {}\n",
                           event.vad_segment->start_ms,
                           event.vad_segment->end_ms,
                           text);
            } else {
                std::print("\n[流最终] {}\n", text);
            }
        }
    });

    asr.on_status([&](const std::string& status) {
        if (verbose) {
            std::lock_guard lock(transcript_mutex);
            if (inline_partial_visible) {
                std::print("\n");
                inline_partial_visible = false;
            }
            std::print("[状态] {}\n", status);
        }
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

    if (verbose) {
        std::print("开始流式识别...\n");
    }
    asr.start();
    asr.finish();

    if (verbose) {
        std::print("通过 Engine 内置 source 编排推送 10ms AudioFrame...\n");
        std::print("等待输入结束事件后执行 stop() 完成收尾...\n\n");
    }

    {
        std::unique_lock lock(status_mutex);
        const bool drained = status_cv.wait_for(lock, std::chrono::seconds(30), [&]() {
            return stream_drained_seen;
        });
        if (!drained) {
            status_cv.wait_for(lock, std::chrono::seconds(5), [&]() {
                return input_eof_seen || asr.input_eof_reached();
            });
        }
    }

    asr.stop();

    RunResult result;
    result.stats = asr.get_stats();
    {
        std::lock_guard lock(transcript_mutex);
        result.final_text = final_transcript;
    }
    return result;
}

struct MetricAgg {
    int count = 0;
    double sum = 0.0;
    double sum_sq = 0.0;
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();

    void add(double value) {
        ++count;
        sum += value;
        sum_sq += value * value;
        min = std::min(min, value);
        max = std::max(max, value);
    }

    double mean() const {
        return count > 0 ? sum / static_cast<double>(count) : 0.0;
    }

    double stddev() const {
        if (count == 0) {
            return 0.0;
        }
        const double m = mean();
        return std::sqrt(std::max(0.0, sum_sq / static_cast<double>(count) - m * m));
    }
};

void print_metric_row(const std::string& name, const MetricAgg& agg, const std::string& unit) {
    std::print("│ {:<20} │ {:>10.2f} │ {:>10.2f} │ {:>10.2f} │ {:>10.2f} │ {:<5} │\n",
               name, agg.mean(), agg.stddev(), agg.min, agg.max, unit);
}

} // namespace

int main(int argc, char* argv[]) {
    std::print("=== Yspeech 流式 ASR 实际音频测试 ===\n\n");

    const auto opts = parse_args(argc, argv);

    std::print("配置文件: {}\n", opts.config_file);
    std::print("音频文件: {}\n", opts.audio_file);
    std::print("source.playback_rate: {}\n", opts.playback_rate);
    std::print("engine.enable_event_queue: {}\n", opts.enable_event_queue ? "true" : "false");
    if (opts.benchmark_runs > 0) {
        std::print("benchmark.runs: {}\n", opts.benchmark_runs);
    }
    std::print("\n");

    try {
        if (opts.benchmark_runs <= 0) {
            auto result = run_once(opts, !opts.quiet);
            if (!opts.quiet && !result.final_text.empty()) {
                std::print("\n最终转写：{}\n", result.final_text);
            }
            std::print("\n{}\n", result.stats.to_string());
            return 0;
        }

        MetricAgg processing_ms;
        MetricAgg non_operator_ms;
        MetricAgg non_operator_share;
        MetricAgg drain_after_eof_ms;
        MetricAgg operator_share;
        MetricAgg rtf;
        MetricAgg stop_overhead_ms;
        MetricAgg stop_monitor_ms;
        MetricAgg stop_source_join_ms;
        MetricAgg stop_finalize_ms;
        MetricAgg stop_drain_events_ms;
        MetricAgg stop_event_join_ms;
        MetricAgg eof_detected_at_ms;
        MetricAgg eof_status_at_ms;
        MetricAgg eof_status_delay_ms;
        MetricAgg event_dispatch_calls;
        MetricAgg event_dispatch_overhead_ms;
        MetricAgg event_queue_push_time_ms;
        MetricAgg event_callback_time_ms;
        MetricAgg event_dispatch_avg_ms;
        MetricAgg event_queue_push_avg_ms;
        MetricAgg event_callback_avg_ms;

        for (int i = 0; i < opts.benchmark_runs; ++i) {
            auto run = run_once(opts, false);
            processing_ms.add(run.stats.total_processing_time_ms);
            non_operator_ms.add(run.stats.non_operator_time_ms);
            non_operator_share.add(run.stats.non_operator_time_percent);
            drain_after_eof_ms.add(run.stats.drain_after_eof_ms);
            operator_share.add(run.stats.operator_time_percent);
            rtf.add(run.stats.rtf);
            stop_overhead_ms.add(run.stats.stop_overhead_ms);
            stop_monitor_ms.add(run.stats.stop_resource_monitor_ms);
            stop_source_join_ms.add(run.stats.stop_source_join_ms);
            stop_finalize_ms.add(run.stats.stop_finalize_stream_ms);
            stop_drain_events_ms.add(run.stats.stop_drain_events_ms);
            stop_event_join_ms.add(run.stats.stop_event_join_ms);
            eof_detected_at_ms.add(run.stats.eof_detected_at_ms);
            eof_status_at_ms.add(run.stats.eof_status_emitted_at_ms);
            eof_status_delay_ms.add(run.stats.eof_status_delay_ms);
            event_dispatch_calls.add(static_cast<double>(run.stats.event_dispatch_calls));
            event_dispatch_overhead_ms.add(run.stats.event_dispatch_overhead_ms);
            event_queue_push_time_ms.add(run.stats.event_queue_push_time_ms);
            event_callback_time_ms.add(run.stats.event_callback_time_ms);
            event_dispatch_avg_ms.add(run.stats.event_dispatch_avg_ms);
            event_queue_push_avg_ms.add(run.stats.event_queue_push_avg_ms);
            event_callback_avg_ms.add(run.stats.event_callback_avg_ms);
            std::print("run {:>2}/{}: processing={:.2f}ms non_op={:.2f}ms ({:.2f}%) drain={:.2f}ms\n",
                       i + 1,
                       opts.benchmark_runs,
                       run.stats.total_processing_time_ms,
                       run.stats.non_operator_time_ms,
                       run.stats.non_operator_time_percent,
                       run.stats.drain_after_eof_ms);
        }

        std::print("\n=== Benchmark Summary ===\n");
        std::print("┌──────────────────────┬────────────┬────────────┬────────────┬────────────┬───────┐\n");
        std::print("│ Metric               │ Mean       │ StdDev     │ Min        │ Max        │ Unit  │\n");
        std::print("├──────────────────────┼────────────┼────────────┼────────────┼────────────┼───────┤\n");
        print_metric_row("Processing Time", processing_ms, "ms");
        print_metric_row("Non-Operator Time", non_operator_ms, "ms");
        print_metric_row("Non-Operator Share", non_operator_share, "%");
        print_metric_row("Drain After EOF", drain_after_eof_ms, "ms");
        print_metric_row("Operator Share", operator_share, "%");
        print_metric_row("RTF", rtf, "-");
        print_metric_row("Stop Overhead", stop_overhead_ms, "ms");
        print_metric_row("Stop.Monitor", stop_monitor_ms, "ms");
        print_metric_row("Stop.SourceJoin", stop_source_join_ms, "ms");
        print_metric_row("Stop.Finalize", stop_finalize_ms, "ms");
        print_metric_row("Stop.DrainEvents", stop_drain_events_ms, "ms");
        print_metric_row("Stop.EventJoin", stop_event_join_ms, "ms");
        print_metric_row("EOF Detected At", eof_detected_at_ms, "ms");
        print_metric_row("EOF Status At", eof_status_at_ms, "ms");
        print_metric_row("EOF Status Delay", eof_status_delay_ms, "ms");
        print_metric_row("Event Dispatch Calls", event_dispatch_calls, "cnt");
        print_metric_row("Event Dispatch Time", event_dispatch_overhead_ms, "ms");
        print_metric_row("Event QueuePush Time", event_queue_push_time_ms, "ms");
        print_metric_row("Event Callback Time", event_callback_time_ms, "ms");
        print_metric_row("Event Dispatch Avg", event_dispatch_avg_ms, "ms");
        print_metric_row("Event QueuePush Avg", event_queue_push_avg_ms, "ms");
        print_metric_row("Event Callback Avg", event_callback_avg_ms, "ms");
        std::print("└──────────────────────┴────────────┴────────────┴────────────┴────────────┴───────┘\n");

    } catch (const std::exception& e) {
        std::println("错误：{}", e.what());
        return 1;
    }

    return 0;
}
