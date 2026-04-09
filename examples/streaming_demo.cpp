#include <nlohmann/json.hpp>

import std;
import yspeech.engine;
import yspeech.log;
import yspeech.types;

namespace {

struct DemoOptions {
    std::string config_file = "examples/configs/streaming_paraformer_asr.json";
    std::string audio_file = "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav";
    double playback_rate = 0.0;
    bool enable_event_queue = true;
    std::optional<std::string> execution_provider_override;
    std::optional<bool> coreml_ane_only_override;
    int benchmark_runs = 0;
    bool quiet = false;
};

auto parse_bool_flag(const std::string& value) -> bool {
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

void print_help(const char* program) {
    std::println("用法: {} [config] [audio] [playback_rate] [选项]", program);
    std::println("");
    std::println("位置参数:");
    std::println("  config         配置文件路径");
    std::println("  audio          音频文件路径");
    std::println("  playback_rate  音频推送倍率，1.0 为实时，20 为 20 倍速");
    std::println("                 默认配置是单线流式 ASR，默认倍率为 0.0");
    std::println("");
    std::println("选项:");
    std::println("  --queue <bool>        显式设置 internal event queue");
    std::println("  --ep <cpu|coreml>     覆盖算子的 execution_provider（benchmark 常用）");
    std::println("  --ane-only <bool>     与 --ep coreml 搭配，覆盖 coreml_ane_only");
    std::println("  --benchmark <N>       连续运行 N 次并输出统计");
    std::println("  --quiet               减少过程输出（benchmark 默认开启）");
    std::println("  --help                显示帮助");
}

auto parse_args(int argc, char* argv[]) -> DemoOptions {
    DemoOptions opts;
    int positional_index = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            print_help(argv[0]);
            std::exit(0);
        }
        if (arg == "--config" && i + 1 < argc) {
            opts.config_file = argv[++i];
            continue;
        }
        if (arg == "--audio" && i + 1 < argc) {
            opts.audio_file = argv[++i];
            continue;
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
        if (arg == "--ep" && i + 1 < argc) {
            std::string ep = argv[++i];
            std::ranges::transform(ep, ep.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (ep == "cpu" || ep == "coreml") {
                opts.execution_provider_override = ep;
            } else {
                throw std::invalid_argument(std::format("Unsupported --ep value: {}", ep));
            }
            continue;
        }
        if (arg == "--ane-only" && i + 1 < argc) {
            opts.coreml_ane_only_override = parse_bool_flag(argv[++i]);
            continue;
        }
        if (!arg.empty() && arg[0] != '-') {
            if (positional_index == 0) {
                opts.config_file = arg;
            } else if (positional_index == 1) {
                opts.audio_file = arg;
            } else if (positional_index == 2) {
                opts.playback_rate = std::stod(arg);
            }
            ++positional_index;
            continue;
        }
        throw std::invalid_argument(std::format("Unknown argument: {}", arg));
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

struct EffectiveConfig {
    std::string path;
    bool temporary = false;
};

enum class DemoProfileKind {
    StreamingLinearAsr,
    StreamingDagAsr,
    StreamingVadOnly,
    UnsupportedOffline,
    UnsupportedOther
};

struct DemoConfigProfile {
    std::string name = "unknown";
    std::string task = "asr";
    std::string mode = "streaming";
    DemoProfileKind kind = DemoProfileKind::UnsupportedOther;
    bool uses_multi_path_graph = false;
    bool has_asr = false;
    bool has_vad = false;
};

auto inspect_demo_config(const std::string& config_path) -> DemoConfigProfile {
    std::ifstream in(config_path);
    if (!in.is_open()) {
        throw std::runtime_error(std::format("Failed to open config: {}", config_path));
    }

    nlohmann::json config;
    in >> config;

    DemoConfigProfile profile;
    profile.name = config.value("name", profile.name);
    profile.task = config.value("task", profile.task);
    profile.mode = config.value("mode", profile.mode);

    if (config.contains("pipelines") && config["pipelines"].is_array()) {
        for (const auto& stage : config["pipelines"]) {
            if (stage.contains("depends_on") && stage["depends_on"].is_array() && !stage["depends_on"].empty()) {
                profile.uses_multi_path_graph = true;
            }
            if (!stage.contains("ops") || !stage["ops"].is_array()) {
                continue;
            }
            for (const auto& op : stage["ops"]) {
                if (!op.contains("name") || !op["name"].is_string()) {
                    continue;
                }
                const auto op_name = op["name"].get<std::string>();
                if (op_name == "SileroVad") {
                    profile.has_vad = true;
                }
                if (op_name == "AsrParaformer" || op_name == "AsrSenseVoice" || op_name == "AsrWhisper") {
                    profile.has_asr = true;
                }
            }
        }
    }

    if (profile.mode == "offline") {
        profile.kind = DemoProfileKind::UnsupportedOffline;
    } else if (profile.task == "vad" && profile.has_vad && !profile.has_asr) {
        profile.kind = DemoProfileKind::StreamingVadOnly;
    } else if (profile.mode == "streaming" && profile.has_asr) {
        profile.kind = profile.uses_multi_path_graph
            ? DemoProfileKind::StreamingDagAsr
            : DemoProfileKind::StreamingLinearAsr;
    }

    return profile;
}

void validate_demo_profile(const DemoConfigProfile& profile) {
    if (profile.kind == DemoProfileKind::UnsupportedOffline) {
        throw std::invalid_argument(
            "streaming_demo 只接受 streaming 配置；offline 配置请使用 simple_transcribe 或 transcribe_tool"
        );
    }
    if (profile.kind == DemoProfileKind::UnsupportedOther) {
        throw std::invalid_argument(
            "streaming_demo 当前只支持 streaming ASR 或 VAD-only 配置"
        );
    }
}

auto profile_label(const DemoConfigProfile& profile) -> std::string {
    switch (profile.kind) {
    case DemoProfileKind::StreamingLinearAsr:
        return "Streaming Linear ASR";
    case DemoProfileKind::StreamingDagAsr:
        return "Streaming DAG ASR";
    case DemoProfileKind::StreamingVadOnly:
        return "Streaming VAD Only";
    case DemoProfileKind::UnsupportedOffline:
        return "Unsupported Offline";
    case DemoProfileKind::UnsupportedOther:
        return "Unsupported";
    }
    return "Unsupported";
}

auto build_effective_config(const DemoOptions& opts) -> EffectiveConfig {
    if (!opts.execution_provider_override.has_value() && !opts.coreml_ane_only_override.has_value()) {
        return {.path = opts.config_file, .temporary = false};
    }

    std::ifstream in(opts.config_file);
    if (!in.is_open()) {
        throw std::runtime_error(std::format("Failed to open config: {}", opts.config_file));
    }

    nlohmann::json config;
    in >> config;

    if (config.contains("pipelines") && config["pipelines"].is_array()) {
        for (auto& pipeline : config["pipelines"]) {
            if (!pipeline.contains("ops") || !pipeline["ops"].is_array()) {
                continue;
            }
            for (auto& op : pipeline["ops"]) {
                if (!op.contains("params") || !op["params"].is_object()) {
                    continue;
                }
                auto& params = op["params"];
                if (opts.execution_provider_override.has_value()) {
                    params["execution_provider"] = *opts.execution_provider_override;
                }
                if (opts.coreml_ane_only_override.has_value()) {
                    params["coreml_ane_only"] = *opts.coreml_ane_only_override;
                }
            }
        }
    }

    const auto temp_dir = std::filesystem::temp_directory_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto tid_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const auto file = temp_dir / std::format("yspeech_streaming_demo_cfg_{}_{}.json", tid_hash, stamp);
    std::ofstream out(file);
    if (!out.is_open()) {
        throw std::runtime_error(std::format("Failed to write temp config: {}", file.string()));
    }
    out << config.dump(2);
    return {.path = file.string(), .temporary = true};
}

auto run_once(const DemoOptions& opts, bool verbose) -> RunResult {
    const auto profile = inspect_demo_config(opts.config_file);
    validate_demo_profile(profile);
    const auto effective_config = build_effective_config(opts);

    yspeech::EngineConfigOptions engine_opts;
    engine_opts.log_level = "warn";
    engine_opts.audio_path = opts.audio_file;
    engine_opts.playback_rate = opts.playback_rate;
    engine_opts.enable_event_queue = opts.enable_event_queue;

    yspeech::Engine asr(effective_config.path, engine_opts);
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
    bool stream_final_printed = false;
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

    auto merge_partial_with_prefix = [&](const std::string& partial_text) {
        return partial_text;
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
            auto merged_text = merge_partial_with_prefix(text);
            if (merged_text == latest_partial || merged_text == latest_segment_final || merged_text == final_transcript) {
                return;
            }

            latest_partial = merged_text;
            std::string padded = merged_text;
            if (rendered_transcript.size() > padded.size()) {
                padded.append(rendered_transcript.size() - padded.size(), ' ');
            }
            rendered_transcript = merged_text;
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
                stream_final_printed = true;
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
            std::string fallback_stream_final;
            {
                std::lock_guard lock(transcript_mutex);
                if (inline_partial_visible) {
                    std::print("\n");
                    inline_partial_visible = false;
                }
                if (status == "stream_drained" && !stream_final_printed && !final_transcript.empty()) {
                    fallback_stream_final = final_transcript;
                    stream_final_printed = true;
                    last_block_text = final_transcript;
                }
            }
            if (!fallback_stream_final.empty()) {
                std::print("[流最终] {}\n", fallback_stream_final);
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
        if (profile.kind == DemoProfileKind::StreamingVadOnly) {
            std::print("开始流式 VAD 检测...\n");
        } else {
            std::print("开始流式识别...\n");
        }
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
    if (effective_config.temporary) {
        std::error_code ec;
        std::filesystem::remove(effective_config.path, ec);
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

    try {
        const auto profile = inspect_demo_config(opts.config_file);
        validate_demo_profile(profile);

        std::print("配置文件: {}\n", opts.config_file);
        std::print("配置名称: {}\n", profile.name);
        std::print("配置画像: {}\n", profile_label(profile));
        std::print("config.mode: {}\n", profile.mode);
        std::print("config.task: {}\n", profile.task);
        std::print("音频文件: {}\n", opts.audio_file);
        std::print("source.playback_rate: {}\n", opts.playback_rate);
        std::print("engine.enable_event_queue: {}\n", opts.enable_event_queue ? "true" : "false");
        if (profile.kind == DemoProfileKind::StreamingDagAsr) {
            std::print("说明: 该配置会走静态 DAG 路径，用于验证 branch/join。\n");
        }
        if (profile.kind == DemoProfileKind::StreamingVadOnly) {
            std::print("说明: 该配置只输出 VAD 事件，不会产生 ASR 文本。\n");
        }
        if (opts.execution_provider_override.has_value()) {
            std::print("override.execution_provider: {}\n", *opts.execution_provider_override);
        }
        if (opts.coreml_ane_only_override.has_value()) {
            std::print("override.coreml_ane_only: {}\n", *opts.coreml_ane_only_override ? "true" : "false");
        }
        if (opts.benchmark_runs > 0) {
            std::print("benchmark.runs: {}\n", opts.benchmark_runs);
        }
        std::print("\n");

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
        MetricAgg first_partial_ms;
        MetricAgg first_final_ms;
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
            first_partial_ms.add(run.stats.time_to_first_partial_ms);
            first_final_ms.add(run.stats.time_to_first_final_ms);
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
        print_metric_row("First Partial", first_partial_ms, "ms");
        print_metric_row("First Final", first_final_ms, "ms");
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
