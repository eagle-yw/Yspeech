module;

#include <nlohmann/json.hpp>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <memory>

export module yspeech.offline_asr;

import std;
import yspeech.context;
import yspeech.pipeline_manager;
import yspeech.types;
import yspeech.log;
import yspeech.error;
import yspeech.pipeline_config;
import yspeech.audio.file;
import yspeech.op.feature.kaldi_fbank;
import yspeech.op.asr.paraformer;

namespace yspeech {

export class OfflineAsr {
public:
    explicit OfflineAsr(const std::string& config_path);
    explicit OfflineAsr(const nlohmann::json& config);
    ~OfflineAsr();
    
    OfflineAsr(const OfflineAsr&) = delete;
    OfflineAsr& operator=(const OfflineAsr&) = delete;
    OfflineAsr(OfflineAsr&&) = delete;
    OfflineAsr& operator=(OfflineAsr&&) = delete;
    
    AsrResult transcribe(const std::string& audio_file);
    std::vector<AsrResult> transcribe_file(const std::string& audio_file);
    
    ProcessingStats get_stats() const;
    const nlohmann::json& get_config() const;
    std::string get_config_path() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class OfflineAsr::Impl {
public:
    Impl() = default;
    
    explicit Impl(const std::string& config_path) {
        config_path_ = config_path;
        load_config();
        init_components();
    }
    
    explicit Impl(const nlohmann::json& config) {
        config_ = config;
        init_components();
    }
    
    ~Impl() = default;

    void load_config() {
        if (!std::filesystem::exists(config_path_)) {
            throw std::runtime_error(std::format("Configuration file not found: {}", config_path_));
        }
        
        std::ifstream file(config_path_);
        if (!file.is_open()) {
            throw std::runtime_error(std::format("Failed to open configuration file: {}", config_path_));
        }
        
        try {
            config_ = nlohmann::json::parse(file);
        } catch (const nlohmann::json::parse_error& e) {
            throw std::runtime_error(std::format("JSON parse error: {}", e.what()));
        }
        
        log_info("Loaded configuration from: {}", config_path_);
    }
    
    void init_components() {
        try {
            pipeline_config_ = PipelineConfig::from_json(config_);
            pipeline_manager_ = std::make_unique<PipelineManager>();
            pipeline_manager_->build(pipeline_config_);
            
            context_ = std::make_unique<Context>();
            
            log_info("OfflineAsr initialized successfully");
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("Failed to initialize components: {}", e.what()));
        }
    }

    AsrResult transcribe(const std::string& audio_file) {
        auto results = transcribe_file(audio_file);
        if (results.empty()) {
            return AsrResult{};
        }
        
        AsrResult combined;
        for (const auto& result : results) {
            if (combined.text.empty()) {
                combined = result;
            } else {
                combined.text += " " + result.text;
                combined.words.insert(combined.words.end(), 
                                    result.words.begin(), 
                                    result.words.end());
            }
        }
        
        return combined;
    }
    
    std::vector<AsrResult> transcribe_file(const std::string& audio_file) {
        std::vector<AsrResult> results;
        
        if (!std::filesystem::exists(audio_file)) {
            log_error("Audio file not found: {}", audio_file);
            return results;
        }
        
        auto start_time = std::chrono::steady_clock::now();
        
        try {
            AudioFileStream audio_stream(audio_file);
            int sample_rate = static_cast<int>(audio_stream.sampleRate());
            int num_channels = audio_stream.micNum();
            
            context_->init_audio_buffer("audio_planar", num_channels, sample_rate * 60);
            
            std::vector<Byte> buffer(4096);
            std::vector<float> all_samples;
            
            while (true) {
                Size bytes_read = audio_stream.read(buffer.data(), buffer.size());
                if (bytes_read == 0) break;
                
                const int16_t* pcm = reinterpret_cast<const int16_t*>(buffer.data());
                size_t num_samples = bytes_read / sizeof(int16_t);
                
                for (size_t i = 0; i < num_samples; ++i) {
                    all_samples.push_back(static_cast<float>(pcm[i]) / 32768.0f);
                }
            }
            
            size_t num_frames = all_samples.size() / num_channels;
            context_->audio_buffer_write_interleaved("audio_planar", all_samples.data(), num_frames, sample_rate);
            
            log_info("Loaded audio: {} frames, {} channels, {}Hz", num_frames, num_channels, sample_rate);
            
        } catch (const std::exception& e) {
            log_error("Failed to load audio file: {}", e.what());
            return results;
        }
        
        context_->set("audio_file", audio_file);
        
        pipeline_manager_->run(*context_);
        
        auto end_time = std::chrono::steady_clock::now();
        stats_.processing_time_ms = std::chrono::duration<double, std::milli>(
            end_time - start_time).count();
        
        if (context_->contains("asr_results")) {
            try {
                results = context_->get<std::vector<AsrResult>>("asr_results");
            } catch (...) {
                log_warn("Failed to get asr_results from context");
            }
        }
        
        log_info("Processed file {}: {} results, {}ms", 
                audio_file, results.size(), stats_.processing_time_ms);
        
        return results;
    }
    
    ProcessingStats get_stats() const {
        return stats_;
    }

    const nlohmann::json& get_config() const {
        return config_;
    }
    
    std::string get_config_path() const {
        return config_path_;
    }

private:
    std::string config_path_;
    nlohmann::json config_;
    PipelineConfig pipeline_config_;
    std::unique_ptr<PipelineManager> pipeline_manager_;
    std::unique_ptr<Context> context_;
    ProcessingStats stats_;
};

OfflineAsr::OfflineAsr(const std::string& config_path)
    : impl_(std::make_unique<Impl>(config_path)) {}

OfflineAsr::OfflineAsr(const nlohmann::json& config)
    : impl_(std::make_unique<Impl>(config)) {}

OfflineAsr::~OfflineAsr() = default;

AsrResult OfflineAsr::transcribe(const std::string& audio_file) {
    return impl_->transcribe(audio_file);
}

std::vector<AsrResult> OfflineAsr::transcribe_file(const std::string& audio_file) {
    return impl_->transcribe_file(audio_file);
}

ProcessingStats OfflineAsr::get_stats() const {
    return impl_->get_stats();
}

const nlohmann::json& OfflineAsr::get_config() const {
    return impl_->get_config();
}

std::string OfflineAsr::get_config_path() const {
    return impl_->get_config_path();
}

export OfflineAsr create_offline_asr(const std::string& config_path) {
    return OfflineAsr(config_path);
}

export OfflineAsr create_offline_asr(const nlohmann::json& config) {
    return OfflineAsr(config);
}

export AsrResult transcribe(const std::string& config_path, const std::string& audio_file) {
    OfflineAsr asr(config_path);
    return asr.transcribe(audio_file);
}

export std::vector<AsrResult> transcribe_file(const std::string& config_path, const std::string& audio_file) {
    OfflineAsr asr(config_path);
    return asr.transcribe_file(audio_file);
}

} // namespace yspeech
