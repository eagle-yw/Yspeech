module;

#include <nlohmann/json.hpp>
#include <kaldi-native-fbank/csrc/online-feature.h>

export module yspeech.domain.feature.kaldi_fbank;

import std;
import yspeech.domain.feature.base;
import yspeech.log;

namespace yspeech {

namespace {

struct CmvnStatsCacheEntry {
    std::vector<float> means;
    std::vector<float> vars;
};

auto cmvn_cache() -> std::unordered_map<std::string, CmvnStatsCacheEntry>& {
    static std::unordered_map<std::string, CmvnStatsCacheEntry> cache;
    return cache;
}

auto cmvn_cache_mutex() -> std::mutex& {
    static std::mutex mutex;
    return mutex;
}

} // namespace

export class KaldiFbankCore : public FeatureCoreIface {
public:
    KaldiFbankCore() = default;

    void init(const nlohmann::json& config) override {
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
        
        log_debug("KaldiFbankCore initialized with KNF: num_bins={}, sample_rate={}, frame_shift={}ms, lfr={}/{}",
                 opts.mel_opts.num_bins, opts.frame_opts.samp_freq, opts.frame_opts.frame_shift_ms,
                 lfr_window_size_, lfr_window_shift_);
    }

    auto process_samples(std::span<const float> audio, bool eos = false) -> std::optional<KaldiFbankOutput> override {
        if (!fbank_) {
            return std::nullopt;
        }
        if (!audio.empty()) {
            fbank_->AcceptWaveform(opts_.frame_opts.samp_freq, audio.data(), static_cast<int32_t>(audio.size()));
        }
        if (eos) {
            fbank_->InputFinished();
            eos_seen_ = true;
        }
        return build_output();
    }

    void deinit() override {
        accumulated_features_.clear();
        lfr_feature_buffer_.clear();
        fbank_.reset();
        frames_read_ = 0;
        lfr_next_start_ = 0;
        output_version_ = 0;
        eos_seen_ = false;
    }

    int feature_dim() const {
        return fbank_ ? fbank_->Dim() : opts_.mel_opts.num_bins;
    }

    float frame_shift() const {
        return opts_.frame_opts.frame_shift_ms / 1000.0f;
    }

private:
    auto build_output() -> std::optional<KaldiFbankOutput> {
        int32_t num_frames = fbank_->NumFramesReady();
        int32_t frames_to_read = num_frames - frames_read_;

        if (frames_to_read <= 0) {
            return std::nullopt;
        }

        std::vector<std::vector<float>> features;
        features.reserve(frames_to_read);

        for (int32_t i = 0; i < frames_to_read; ++i) {
            const float* frame_data = fbank_->GetFrame(frames_read_ + i);
            std::vector<float> frame(frame_data, frame_data + fbank_->Dim());
            features.push_back(std::move(frame));
        }

        frames_read_ += frames_to_read;
        features = apply_lfr(features);

        if (!cmvn_means_.empty() || !cmvn_vars_.empty()) {
            apply_cmvn(features);
        }

        if (features.empty()) {
            return std::nullopt;
        }

        auto delta_features = features;
        const auto delta_frame_count = static_cast<int>(delta_features.size());

        if (enable_accumulation_) {
            accumulated_features_.insert(accumulated_features_.end(), features.begin(), features.end());
            if (accumulated_features_.size() < static_cast<size_t>(min_accumulated_frames_)) {
                return std::nullopt;
            }
            if (accumulated_features_.size() > static_cast<size_t>(max_accumulated_frames_)) {
                accumulated_features_.erase(
                    accumulated_features_.begin(),
                    accumulated_features_.end() - max_accumulated_frames_);
            }
            features = accumulated_features_;
        }

        return KaldiFbankOutput{
            .delta_features = std::move(delta_features),
            .features = std::move(features),
            .delta_num_frames = delta_frame_count,
            .num_frames = enable_accumulation_
                ? static_cast<int>(accumulated_features_.size())
                : static_cast<int>(frames_to_read),
            .accumulated_num_frames = enable_accumulation_
                ? static_cast<int>(accumulated_features_.size())
                : delta_frame_count,
            .num_bins = fbank_->Dim() * lfr_window_size_,
            .version = static_cast<std::uint64_t>(++output_version_)
        };
    }

    std::unique_ptr<knf::OnlineFbank> fbank_;
    knf::FbankOptions opts_;
    std::vector<float> cmvn_means_;
    std::vector<float> cmvn_vars_;
    bool normalize_means_ = true;
    bool normalize_vars_ = true;
    int lfr_window_size_ = 1;
    int lfr_window_shift_ = 1;
    int32_t frames_read_ = 0;
    bool enable_accumulation_ = false;
    int min_accumulated_frames_ = 15;
    int max_accumulated_frames_ = 100;
    std::vector<std::vector<float>> accumulated_features_;
    std::vector<std::vector<float>> lfr_feature_buffer_;
    std::size_t lfr_next_start_ = 0;
    std::uint64_t output_version_ = 0;
    bool eos_seen_ = false;

    void load_cmvn_stats() {
        if (cmvn_file_.empty()) return;
        std::lock_guard lock(cmvn_cache_mutex());
        if (auto it = cmvn_cache().find(cmvn_file_); it != cmvn_cache().end()) {
            cmvn_means_ = it->second.means;
            cmvn_vars_ = it->second.vars;
            return;
        }
        
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
                cmvn_cache()[cmvn_file_] = CmvnStatsCacheEntry{
                    .means = cmvn_means_,
                    .vars = cmvn_vars_
                };
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
    
    auto apply_lfr(const std::vector<std::vector<float>>& features) -> std::vector<std::vector<float>> {
        if (features.empty()) {
            return features;
        }

        if (lfr_window_size_ <= 1) {
            return features;
        }

        lfr_feature_buffer_.insert(lfr_feature_buffer_.end(), features.begin(), features.end());

        const int num_bins = static_cast<int>(lfr_feature_buffer_.front().size());
        std::vector<std::vector<float>> lfr_features;
        while (lfr_next_start_ + static_cast<std::size_t>(lfr_window_size_) <= lfr_feature_buffer_.size()) {
            std::vector<float> lfr_frame;
            lfr_frame.reserve(num_bins * lfr_window_size_);

            for (int w = 0; w < lfr_window_size_; ++w) {
                const auto& source_frame = lfr_feature_buffer_[lfr_next_start_ + static_cast<std::size_t>(w)];
                lfr_frame.insert(lfr_frame.end(), source_frame.begin(), source_frame.end());
            }

            lfr_features.push_back(std::move(lfr_frame));
            lfr_next_start_ += static_cast<std::size_t>(lfr_window_shift_);
        }

        if (lfr_next_start_ > 0) {
            const std::size_t keep_from =
                lfr_next_start_ > static_cast<std::size_t>(lfr_window_shift_)
                    ? lfr_next_start_ - static_cast<std::size_t>(lfr_window_shift_)
                    : 0;
            if (keep_from > 0) {
                lfr_feature_buffer_.erase(
                    lfr_feature_buffer_.begin(),
                    lfr_feature_buffer_.begin() + static_cast<std::ptrdiff_t>(keep_from)
                );
                lfr_next_start_ -= keep_from;
            }
        }

        return lfr_features;
    }

    std::string cmvn_file_;
};

namespace {

FeatureCoreRegistrar<KaldiFbankCore> kaldi_fbank_core_registrar("KaldiFbank");

}

}
