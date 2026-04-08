module;

#include <nlohmann/json.hpp>

export module yspeech.performance_export;

import std;
import yspeech.types;

namespace yspeech {

export class PerformanceExporter {
public:
    static std::string to_json(const ProcessingStats& stats) {
        nlohmann::json j;
        
        j["audio_chunks_processed"] = stats.audio_chunks_processed;
        j["speech_segments_detected"] = stats.speech_segments_detected;
        j["asr_results_generated"] = stats.asr_results_generated;
        j["engine_init_time_ms"] = stats.engine_init_time_ms;
        j["total_processing_time_ms"] = stats.total_processing_time_ms;
        j["operator_total_time_ms"] = stats.operator_total_time_ms;
        j["non_operator_time_ms"] = stats.non_operator_time_ms;
        j["operator_time_percent"] = stats.operator_time_percent;
        j["non_operator_time_percent"] = stats.non_operator_time_percent;
        j["time_to_first_chunk_ms"] = stats.time_to_first_chunk_ms;
        j["drain_after_eof_ms"] = stats.drain_after_eof_ms;
        j["stop_overhead_ms"] = stats.stop_overhead_ms;
        j["stop_resource_monitor_ms"] = stats.stop_resource_monitor_ms;
        j["stop_source_join_ms"] = stats.stop_source_join_ms;
        j["stop_finalize_stream_ms"] = stats.stop_finalize_stream_ms;
        j["stop_drain_events_ms"] = stats.stop_drain_events_ms;
        j["stop_event_join_ms"] = stats.stop_event_join_ms;
        j["eof_detected_at_ms"] = stats.eof_detected_at_ms;
        j["eof_status_emitted_at_ms"] = stats.eof_status_emitted_at_ms;
        j["eof_status_delay_ms"] = stats.eof_status_delay_ms;
        j["event_dispatch_calls"] = stats.event_dispatch_calls;
        j["event_dispatch_overhead_ms"] = stats.event_dispatch_overhead_ms;
        j["event_queue_push_time_ms"] = stats.event_queue_push_time_ms;
        j["event_callback_time_ms"] = stats.event_callback_time_ms;
        j["event_dispatch_avg_ms"] = stats.event_dispatch_avg_ms;
        j["event_queue_push_avg_ms"] = stats.event_queue_push_avg_ms;
        j["event_callback_avg_ms"] = stats.event_callback_avg_ms;
        j["audio_duration_ms"] = stats.audio_duration_ms;
        j["rtf"] = stats.rtf;
        j["peak_memory_mb"] = stats.peak_memory_mb;
        j["avg_cpu_percent"] = stats.avg_cpu_percent;
        
        if (!stats.operator_timings.empty()) {
            nlohmann::json timings_json = nlohmann::json::array();
            for (const auto& [id, timing] : stats.operator_timings) {
                nlohmann::json t;
                t["op_id"] = timing.op_id;
                t["total_time_ms"] = timing.total_time_ms;
                t["min_time_ms"] = timing.min_time_ms == std::numeric_limits<double>::max() ? 0.0 : timing.min_time_ms;
                t["max_time_ms"] = timing.max_time_ms;
                t["avg_time_ms"] = timing.avg_time_ms;
                t["call_count"] = timing.call_count;
                t["effective_call_count"] = timing.effective_call_count;
                t["effective_avg_time_ms"] = timing.effective_avg_time_ms;
                
                if (!timing.recent_times_ms.empty()) {
                    t["p50_ms"] = timing.p50();
                    t["p95_ms"] = timing.p95();
                    t["p99_ms"] = timing.p99();
                }
                
                timings_json.push_back(t);
            }
            j["operator_timings"] = timings_json;
        }
        
        return j.dump(2);
    }
    
    static std::string to_csv(const ProcessingStats& stats) {
        std::ostringstream oss;
        
        oss << "metric,value\n";
        oss << "audio_chunks_processed," << stats.audio_chunks_processed << "\n";
        oss << "speech_segments_detected," << stats.speech_segments_detected << "\n";
        oss << "asr_results_generated," << stats.asr_results_generated << "\n";
        oss << "engine_init_time_ms," << std::fixed << std::setprecision(2) << stats.engine_init_time_ms << "\n";
        oss << "total_processing_time_ms," << std::fixed << std::setprecision(2) << stats.total_processing_time_ms << "\n";
        oss << "operator_total_time_ms," << std::fixed << std::setprecision(2) << stats.operator_total_time_ms << "\n";
        oss << "non_operator_time_ms," << std::fixed << std::setprecision(2) << stats.non_operator_time_ms << "\n";
        oss << "operator_time_percent," << std::fixed << std::setprecision(2) << stats.operator_time_percent << "\n";
        oss << "non_operator_time_percent," << std::fixed << std::setprecision(2) << stats.non_operator_time_percent << "\n";
        oss << "time_to_first_chunk_ms," << std::fixed << std::setprecision(2) << stats.time_to_first_chunk_ms << "\n";
        oss << "drain_after_eof_ms," << std::fixed << std::setprecision(2) << stats.drain_after_eof_ms << "\n";
        oss << "stop_overhead_ms," << std::fixed << std::setprecision(2) << stats.stop_overhead_ms << "\n";
        oss << "stop_resource_monitor_ms," << std::fixed << std::setprecision(2) << stats.stop_resource_monitor_ms << "\n";
        oss << "stop_source_join_ms," << std::fixed << std::setprecision(2) << stats.stop_source_join_ms << "\n";
        oss << "stop_finalize_stream_ms," << std::fixed << std::setprecision(2) << stats.stop_finalize_stream_ms << "\n";
        oss << "stop_drain_events_ms," << std::fixed << std::setprecision(2) << stats.stop_drain_events_ms << "\n";
        oss << "stop_event_join_ms," << std::fixed << std::setprecision(2) << stats.stop_event_join_ms << "\n";
        oss << "eof_detected_at_ms," << std::fixed << std::setprecision(2) << stats.eof_detected_at_ms << "\n";
        oss << "eof_status_emitted_at_ms," << std::fixed << std::setprecision(2) << stats.eof_status_emitted_at_ms << "\n";
        oss << "eof_status_delay_ms," << std::fixed << std::setprecision(2) << stats.eof_status_delay_ms << "\n";
        oss << "event_dispatch_calls," << stats.event_dispatch_calls << "\n";
        oss << "event_dispatch_overhead_ms," << std::fixed << std::setprecision(2) << stats.event_dispatch_overhead_ms << "\n";
        oss << "event_queue_push_time_ms," << std::fixed << std::setprecision(2) << stats.event_queue_push_time_ms << "\n";
        oss << "event_callback_time_ms," << std::fixed << std::setprecision(2) << stats.event_callback_time_ms << "\n";
        oss << "event_dispatch_avg_ms," << std::fixed << std::setprecision(4) << stats.event_dispatch_avg_ms << "\n";
        oss << "event_queue_push_avg_ms," << std::fixed << std::setprecision(4) << stats.event_queue_push_avg_ms << "\n";
        oss << "event_callback_avg_ms," << std::fixed << std::setprecision(4) << stats.event_callback_avg_ms << "\n";
        oss << "audio_duration_ms," << std::fixed << std::setprecision(2) << stats.audio_duration_ms << "\n";
        oss << "rtf," << std::fixed << std::setprecision(3) << stats.rtf << "\n";
        oss << "peak_memory_mb," << std::fixed << std::setprecision(2) << stats.peak_memory_mb << "\n";
        oss << "avg_cpu_percent," << std::fixed << std::setprecision(1) << stats.avg_cpu_percent << "\n";
        
        if (!stats.operator_timings.empty()) {
            oss << "\noperator_id,total_time_ms,avg_time_ms,min_time_ms,max_time_ms,call_count,effective_call_count,effective_avg_time_ms,p50_ms,p95_ms,p99_ms\n";
            for (const auto& [id, timing] : stats.operator_timings) {
                oss << timing.op_id << ","
                    << std::fixed << std::setprecision(2) << timing.total_time_ms << ","
                    << std::fixed << std::setprecision(2) << timing.avg_time_ms << ","
                    << std::fixed << std::setprecision(2) << (timing.min_time_ms == std::numeric_limits<double>::max() ? 0.0 : timing.min_time_ms) << ","
                    << std::fixed << std::setprecision(2) << timing.max_time_ms << ","
                    << timing.call_count << ","
                    << timing.effective_call_count << ","
                    << std::fixed << std::setprecision(2) << timing.effective_avg_time_ms << ","
                    << std::fixed << std::setprecision(2) << timing.p50() << ","
                    << std::fixed << std::setprecision(2) << timing.p95() << ","
                    << std::fixed << std::setprecision(2) << timing.p99() << "\n";
            }
        }
        
        return oss.str();
    }
    
    static bool to_file(const ProcessingStats& stats, 
                        const std::string& path,
                        const std::string& format = "json") {
        std::string content;
        if (format == "json") {
            content = to_json(stats);
        } else if (format == "csv") {
            content = to_csv(stats);
        } else {
            return false;
        }
        
        std::ofstream file(path);
        if (!file.is_open()) {
            return false;
        }
        
        file << content;
        return true;
    }
};

}
