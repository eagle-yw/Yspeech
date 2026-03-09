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
        j["total_processing_time_ms"] = stats.total_processing_time_ms;
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
        oss << "total_processing_time_ms," << std::fixed << std::setprecision(2) << stats.total_processing_time_ms << "\n";
        oss << "audio_duration_ms," << std::fixed << std::setprecision(2) << stats.audio_duration_ms << "\n";
        oss << "rtf," << std::fixed << std::setprecision(3) << stats.rtf << "\n";
        oss << "peak_memory_mb," << std::fixed << std::setprecision(2) << stats.peak_memory_mb << "\n";
        oss << "avg_cpu_percent," << std::fixed << std::setprecision(1) << stats.avg_cpu_percent << "\n";
        
        if (!stats.operator_timings.empty()) {
            oss << "\noperator_id,total_time_ms,avg_time_ms,min_time_ms,max_time_ms,call_count,p50_ms,p95_ms,p99_ms\n";
            for (const auto& [id, timing] : stats.operator_timings) {
                oss << timing.op_id << ","
                    << std::fixed << std::setprecision(2) << timing.total_time_ms << ","
                    << std::fixed << std::setprecision(2) << timing.avg_time_ms << ","
                    << std::fixed << std::setprecision(2) << (timing.min_time_ms == std::numeric_limits<double>::max() ? 0.0 : timing.min_time_ms) << ","
                    << std::fixed << std::setprecision(2) << timing.max_time_ms << ","
                    << timing.call_count << ","
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
