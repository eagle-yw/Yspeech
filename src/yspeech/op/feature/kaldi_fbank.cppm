module;

#include <nlohmann/json.hpp>
#include <kaldi-native-fbank/csrc/online-feature.h>
#include <memory>
#include <vector>
#include <string>
#include <fstream>
#include <cmath>

export module yspeech.op.feature.kaldi_fbank;

import std;
import yspeech.context;
import yspeech.op;
import yspeech.log;

namespace yspeech {

export class OpKaldiFbank {
public:
    OpKaldiFbank() = default;

    void init(const nlohmann::json& config) {
        knf::FbankOptions opts;
        
        if (config.contains("num_bins")) {
            opts.mel_opts.num_bins = config["num_bins"].get<int>();
        }
        if (config.contains("sample_rate")) {
            opts.frame_opts.samp_freq = config["sample_rate"].get<float>();
        }
        if (config.contains("frame_length_ms")) {
            opts.frame_opts.frame_length_ms = config["frame_length_ms"].get<float>();
        }
        if (config.contains("frame_shift_ms")) {
            opts.frame_opts.frame_shift_ms = config["frame_shift_ms"].get<float>();
        }
        if (config.contains("low_freq")) {
            opts.mel_opts.low_freq = config["low_freq"].get<float>();
        }
        if (config.contains("high_freq")) {
            opts.mel_opts.high_freq = config["high_freq"].get<float>();
        }
        if (config.contains("preemph_coeff")) {
            opts.frame_opts.preemph_coeff = config["preemph_coeff"].get<float>();
        }
        if (config.contains("dither")) {
            opts.frame_opts.dither = config["dither"].get<float>();
        }
        if (config.contains("energy_floor")) {
            opts.energy_floor = config["energy_floor"].get<float>();
        }
        if (config.contains("window_type")) {
            opts.frame_opts.window_type = config["window_type"].get<std::string>();
        }
        if (config.contains("remove_dc_offset")) {
            opts.frame_opts.remove_dc_offset = config["remove_dc_offset"].get<bool>();
        }
        if (config.contains("snip_edges")) {
            opts.frame_opts.snip_edges = config["snip_edges"].get<bool>();
        }
        
        opts_ = opts;
        fbank_ = std::make_unique<knf::OnlineFbank>(opts);
        
        if (config.contains("input_buffer_key")) {
            input_buffer_key_ = config["input_buffer_key"].get<std::string>();
        }
        if (config.contains("output_key")) {
            output_key_ = config["output_key"].get<std::string>();
        }
        
        if (config.contains("cmvn_file")) {
            cmvn_file_ = config["cmvn_file"].get<std::string>();
            load_cmvn_stats();
        }
        if (config.contains("normalize_means")) {
            normalize_means_ = config["normalize_means"].get<bool>();
        }
        if (config.contains("normalize_vars")) {
            normalize_vars_ = config["normalize_vars"].get<bool>();
        }
        
        if (config.contains("lfr_window_size")) {
            lfr_window_size_ = config["lfr_window_size"].get<int>();
        }
        if (config.contains("lfr_window_shift")) {
            lfr_window_shift_ = config["lfr_window_shift"].get<int>();
        }
        
        if (config.contains("enable_accumulation")) {
            enable_accumulation_ = config["enable_accumulation"].get<bool>();
        }
        if (config.contains("min_accumulated_frames")) {
            min_accumulated_frames_ = config["min_accumulated_frames"].get<int>();
        }
        if (config.contains("max_accumulated_frames")) {
            max_accumulated_frames_ = config["max_accumulated_frames"].get<int>();
        }
        
        log_info("OpKaldiFbank initialized with KNF: num_bins={}, sample_rate={}, frame_shift={}ms, lfr={}/{}",
                 opts.mel_opts.num_bins, opts.frame_opts.samp_freq, opts.frame_opts.frame_shift_ms,
                 lfr_window_size_, lfr_window_shift_);
    }

    void process(Context& ctx) {
        auto audio_buffer = ctx.get_audio_buffer(input_buffer_key_);
        if (!audio_buffer || audio_buffer->channels.empty()) {
            log_debug("No audio buffer available");
            return;
        }

        std::vector<float> audio_data;
        float sample;
        while (audio_buffer->channels[0]->pop(sample)) {
            audio_data.push_back(sample);
        }

        if (audio_data.empty()) {
            return;
        }

        fbank_->AcceptWaveform(opts_.frame_opts.samp_freq, audio_data.data(), static_cast<int32_t>(audio_data.size()));
        
        int32_t num_frames = fbank_->NumFramesReady();
        int32_t frames_to_read = num_frames - frames_read_;
        
        if (frames_to_read <= 0) {
            return;
        }
        
        std::vector<std::vector<float>> features;
        features.reserve(frames_to_read);
        
        for (int32_t i = 0; i < frames_to_read; ++i) {
            const float* frame_data = fbank_->GetFrame(frames_read_ + i);
            std::vector<float> frame(frame_data, frame_data + fbank_->Dim());
            features.push_back(std::move(frame));
        }
        
        frames_read_ += frames_to_read;
        
        if (lfr_window_size_ > 1) {
            features = apply_lfr(features);
        }
        
        if (!cmvn_means_.empty() || !cmvn_vars_.empty()) {
            apply_cmvn(features);
        }
        
        if (enable_accumulation_) {
            accumulated_features_.insert(accumulated_features_.end(), 
                                        features.begin(), features.end());
            
            if (accumulated_features_.size() >= static_cast<size_t>(min_accumulated_frames_)) {
                ctx.set(output_key_ + "_features", accumulated_features_);
                ctx.set(output_key_ + "_num_frames", static_cast<int>(accumulated_features_.size()));
                
                if (accumulated_features_.size() > static_cast<size_t>(max_accumulated_frames_)) {
                    accumulated_features_.erase(
                        accumulated_features_.begin(),
                        accumulated_features_.end() - max_accumulated_frames_);
                }
                
                log_info("Extracted {} frames (accumulated) of {}-dim Fbank features", 
                         accumulated_features_.size(), fbank_->Dim() * lfr_window_size_);
            } else {
                ctx.set(output_key_ + "_features", std::vector<std::vector<float>>{});
                ctx.set(output_key_ + "_num_frames", 0);
                
                log_debug("Accumulating features: {}/{} frames", 
                          accumulated_features_.size(), min_accumulated_frames_);
            }
        } else {
            ctx.set(output_key_ + "_features", features);
            ctx.set(output_key_ + "_num_frames", static_cast<int>(features.size()));
            
            log_info("Extracted {} frames of {}-dim Fbank features (LFR {}/{})", 
                     features.size(), fbank_->Dim() * lfr_window_size_,
                     lfr_window_size_, lfr_window_shift_);
        }
        
        ctx.set(output_key_ + "_num_bins", fbank_->Dim() * lfr_window_size_);
    }

    void deinit() {
        accumulated_features_.clear();
        fbank_.reset();
        frames_read_ = 0;
        log_info("OpKaldiFbank deinitialized");
    }

    int feature_dim() const {
        return fbank_ ? fbank_->Dim() : opts_.mel_opts.num_bins;
    }

    float frame_shift() const {
        return opts_.frame_opts.frame_shift_ms / 1000.0f;
    }

private:
    std::unique_ptr<knf::OnlineFbank> fbank_;
    knf::FbankOptions opts_;
    std::string input_buffer_key_ = "audio_planar";
    std::string output_key_ = "fbank";
    std::string cmvn_file_;
    
    int lfr_window_size_ = 1;
    int lfr_window_shift_ = 1;
    int32_t frames_read_ = 0;
    
    std::vector<float> cmvn_means_;
    std::vector<float> cmvn_vars_;
    bool normalize_means_ = true;
    bool normalize_vars_ = true;
    
    bool enable_accumulation_ = false;
    int min_accumulated_frames_ = 15;
    int max_accumulated_frames_ = 100;
    std::vector<std::vector<float>> accumulated_features_;

    void load_cmvn_stats() {
        if (cmvn_file_.empty()) return;
        
        std::ifstream file(cmvn_file_);
        if (!file.is_open()) {
            log_warn("Failed to open CMVN file: {}", cmvn_file_);
            return;
        }
        
        std::string line;
        int learn_rate_coef_count = 0;
        
        while (std::getline(file, line)) {
            if (line.find("<LearnRateCoef>") == std::string::npos) {
                continue;
            }
            
            size_t bracket_start = line.find('[');
            size_t bracket_end = line.find(']');
            if (bracket_start == std::string::npos || bracket_end == std::string::npos) {
                continue;
            }
            
            std::string values_str = line.substr(bracket_start + 1, bracket_end - bracket_start - 1);
            std::istringstream iss(values_str);
            std::vector<float> values;
            float val;
            while (iss >> val) {
                values.push_back(val);
            }
            
            if (learn_rate_coef_count == 0) {
                cmvn_means_ = std::move(values);
                log_info("Loaded CMVN neg_mean: {} values", cmvn_means_.size());
            } else if (learn_rate_coef_count == 1) {
                cmvn_vars_ = std::move(values);
                log_info("Loaded CMVN inv_std: {} values", cmvn_vars_.size());
                break;
            }
            learn_rate_coef_count++;
        }
    }

    void apply_cmvn(std::vector<std::vector<float>>& features) {
        for (auto& frame : features) {
            for (size_t i = 0; i < frame.size(); ++i) {
                if (normalize_means_ && i < cmvn_means_.size()) {
                    frame[i] += cmvn_means_[i];
                }
                if (normalize_vars_ && i < cmvn_vars_.size()) {
                    frame[i] *= cmvn_vars_[i];
                }
            }
        }
    }
    
    std::vector<std::vector<float>> apply_lfr(const std::vector<std::vector<float>>& features) {
        if (features.empty() || lfr_window_size_ <= 1) {
            return features;
        }
        
        int num_frames = static_cast<int>(features.size());
        int num_bins = static_cast<int>(features[0].size());
        int T = (num_frames - lfr_window_size_) / lfr_window_shift_ + 1;
        
        if (T <= 0) {
            return {};
        }
        
        std::vector<std::vector<float>> lfr_features;
        lfr_features.reserve(T);
        
        for (int t = 0; t < T; ++t) {
            int start = t * lfr_window_shift_;
            std::vector<float> lfr_frame;
            lfr_frame.reserve(num_bins * lfr_window_size_);
            
            for (int w = 0; w < lfr_window_size_; ++w) {
                int frame_idx = start + w;
                if (frame_idx < num_frames) {
                    lfr_frame.insert(lfr_frame.end(), 
                                     features[frame_idx].begin(), 
                                     features[frame_idx].end());
                } else {
                    lfr_frame.insert(lfr_frame.end(), num_bins, 0.0f);
                }
            }
            lfr_features.push_back(std::move(lfr_frame));
        }
        
        return lfr_features;
    }
};

namespace {

OperatorRegistrar<OpKaldiFbank> registrar("KaldiFbank");

}

}
