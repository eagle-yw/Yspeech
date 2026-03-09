module;

#include <nlohmann/json.hpp>

export module yspeech.context;

import std;
import yspeech.error;
import yspeech.state;
import yspeech.types;
import yspeech.ring_buffer;

namespace yspeech {

namespace detail {

class AsyncCallbackQueue {
public:
    using Callback = std::function<void(const Error&)>;
    
    void push(Callback callback, Error error) {
        {
            std::unique_lock lock(mutex_);
            queue_.push({std::move(callback), std::move(error)});
        }
        cv_.notify_one();
    }
    
    void start() {
        running_ = true;
        thread_ = std::thread([this]() {
            while (running_) {
                std::unique_lock lock(mutex_);
                
                cv_.wait(lock, [this]() {
                    return !running_ || !queue_.empty();
                });
                
                if (!running_ && queue_.empty()) break;
                
                while (!queue_.empty()) {
                    auto item = std::move(queue_.front());
                    queue_.pop();
                    lock.unlock();
                    
                    if (item.callback) {
                        try {
                            item.callback(item.error);
                        } catch (...) {
                        }
                    }
                    
                    lock.lock();
                }
            }
        });
    }
    
    void stop() {
        running_ = false;
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    
    ~AsyncCallbackQueue() {
        stop();
    }

private:
    struct QueueItem {
        Callback callback;
        Error error;
    };
    
    std::queue<QueueItem> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}

struct AudioBufferInternal {
    int num_channels;
    int sample_rate = 16000;
    std::vector<std::shared_ptr<RingBuffer<float>>> channels;
};

export class Context {
public:
    using ErrorCallback = std::function<void(const Error&)>;
    
    enum class CallbackMode {
        Sync,
        Async
    };
    
    Context() {
        async_queue_.start();
    }
    
    ~Context() {
        async_queue_.stop();
        stop_data();
    }

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    template <typename T>
    void set(const std::string& key, T&& value) {
        std::unique_lock lock(mutex_);
        data_[key] = std::forward<T>(value);
    }

    template <typename T>
    T& get(const std::string& key) {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
            throw std::out_of_range(std::format("Context key not found: {}", key));
        }
        return std::any_cast<T&>(it->second);
    }
    
    template <typename T>
    const T& get(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
            throw std::out_of_range(std::format("Context key not found: {}", key));
        }
        return std::any_cast<const T&>(it->second);
    }
    
    bool contains(const std::string& key) const {
        std::shared_lock lock(mutex_);
        return data_.contains(key);
    }
    
    std::any get_any(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
             return {};
        }
        return it->second;
    }
    
    void remove(const std::string& key) {
        std::unique_lock lock(mutex_);
        data_.erase(key);
    }
    
    void clear_data() {
        std::unique_lock lock(mutex_);
        data_.clear();
    }

    void record_error(const Error& error) {
        std::unique_lock lock(mutex_);
        errors_.push_back(error);
        state_.mark_error();
        if (error.recovered) {
            state_.mark_recovered();
        }
        if (error_callback_) {
            if (callback_mode_ == CallbackMode::Async) {
                async_queue_.push(error_callback_, error);
            } else {
                lock.unlock();
                error_callback_(error);
            }
        }
    }

    void record_error(const std::string& source, const std::string& message,
                      const std::string& component = "Operator",
                      ErrorCode code = ErrorCode::Unknown,
                      ErrorLevel level = ErrorLevel::Error,
                      int attempt = 0, 
                      bool recovered = false,
                      const nlohmann::json& metadata = nlohmann::json()) {
        Error error{
            .source = source,
            .component = component,
            .message = message,
            .code = code,
            .level = level,
            .attempt = attempt,
            .recovered = recovered,
            .timestamp = std::chrono::system_clock::now(),
            .metadata = metadata
        };
        record_error(error);
    }

    const std::vector<Error>& errors() const {
        std::shared_lock lock(mutex_);
        return errors_;
    }

    std::vector<Error> errors_by_source(const std::string& source) const {
        std::shared_lock lock(mutex_);
        std::vector<Error> result;
        for (const auto& err : errors_) {
            if (err.source == source) {
                result.push_back(err);
            }
        }
        return result;
    }

    std::vector<Error> errors_by_component(const std::string& component) const {
        std::shared_lock lock(mutex_);
        std::vector<Error> result;
        for (const auto& err : errors_) {
            if (err.component == component) {
                result.push_back(err);
            }
        }
        return result;
    }

    std::vector<Error> errors_by_level(ErrorLevel level) const {
        std::shared_lock lock(mutex_);
        std::vector<Error> result;
        for (const auto& err : errors_) {
            if (err.level == level) {
                result.push_back(err);
            }
        }
        return result;
    }

    bool has_errors() const {
        std::shared_lock lock(mutex_);
        return !errors_.empty();
    }

    bool has_fatal_errors() const {
        std::shared_lock lock(mutex_);
        for (const auto& err : errors_) {
            if (err.level == ErrorLevel::Fatal && !err.recovered) {
                return true;
            }
        }
        return false;
    }

    int error_count() const {
        return state_.total_errors.load(std::memory_order_relaxed);
    }

    int recovered_count() const {
        return state_.recovered_errors.load(std::memory_order_relaxed);
    }

    void set_error_callback(ErrorCallback callback, 
                            CallbackMode mode = CallbackMode::Sync) {
        std::unique_lock lock(mutex_);
        error_callback_ = std::move(callback);
        callback_mode_ = mode;
    }
    
    void set_callback_mode(CallbackMode mode) {
        std::unique_lock lock(mutex_);
        callback_mode_ = mode;
    }

    void clear_errors() {
        std::unique_lock lock(mutex_);
        errors_.clear();
        state_.reset();
    }

    State& state() { return state_; }
    const State& state() const { return state_; }

    nlohmann::json errors_to_json() const {
        std::shared_lock lock(mutex_);
        nlohmann::json result = nlohmann::json::array();
        for (const auto& err : errors_) {
            result.push_back(err.to_json());
        }
        return result;
    }

    std::string errors_summary() const {
        std::shared_lock lock(mutex_);
        return std::format("Total: {}, Recovered: {}, Skipped: {}, Fatal: {}",
            state_.total_errors.load(std::memory_order_relaxed),
            state_.recovered_errors.load(std::memory_order_relaxed),
            state_.skipped_operators.load(std::memory_order_relaxed),
            has_fatal_errors() ? "Yes" : "No"
        );
    }

    void notify_data_ready() {
        data_ready_ = true;
        data_cv_.notify_all();
    }
    
    bool wait_for_data(std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        std::unique_lock lock(data_mutex_);
        return data_cv_.wait_for(lock, timeout, [this]() { 
            return data_ready_ || data_stopped_; 
        }) && data_ready_;
    }
    
    void clear_data_ready() {
        data_ready_ = false;
    }
    
    void stop_data() {
        data_stopped_ = true;
        data_cv_.notify_all();
    }
    
    void reset_data_sync() {
        data_ready_ = false;
        data_stopped_ = false;
    }
    
    template<typename T>
    void init_ring_buffer(const std::string& key, size_t capacity) {
        std::unique_lock lock(mutex_);
        data_[key] = std::make_shared<RingBuffer<T>>(capacity);
    }
    
    template<typename T>
    bool ring_buffer_push(const std::string& key, const T& item) {
        auto buf = get_ring_buffer<T>(key);
        if (!buf) return false;
        bool result = buf->push(item);
        if (result) {
            notify_data_ready();
        }
        return result;
    }
    
    template<typename T>
    bool ring_buffer_push(const std::string& key, T&& item) {
        auto buf = get_ring_buffer<T>(key);
        if (!buf) return false;
        bool result = buf->push(std::forward<T>(item));
        if (result) {
            notify_data_ready();
        }
        return result;
    }
    
    template<typename T>
    bool ring_buffer_pop(const std::string& key, T& item) {
        auto buf = get_ring_buffer<T>(key);
        if (!buf) return false;
        return buf->pop(item);
    }
    
    template<typename T>
    bool ring_buffer_pop_wait(const std::string& key, T& item, 
                              std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        auto buf = get_ring_buffer<T>(key);
        if (!buf) return false;
        return buf->pop_wait(item, timeout);
    }
    
    template<typename T>
    std::shared_ptr<RingBuffer<T>> get_ring_buffer(const std::string& key) {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
            return nullptr;
        }
        try {
            return std::any_cast<std::shared_ptr<RingBuffer<T>>>(it->second);
        } catch (...) {
            return nullptr;
        }
    }
    
    void init_audio_buffer(const std::string& key, int num_channels, size_t capacity_samples) {
        std::unique_lock lock(mutex_);
        auto buffer = std::make_shared<AudioBufferInternal>();
        buffer->num_channels = num_channels;
        buffer->channels.reserve(num_channels);
        for (int i = 0; i < num_channels; ++i) {
            buffer->channels.push_back(std::make_shared<RingBuffer<float>>(capacity_samples));
        }
        data_[key] = buffer;
    }
    
    bool audio_buffer_write_interleaved(const std::string& key,
                                        const float* data, size_t num_frames,
                                        int sample_rate, int64_t timestamp_ms = 0) {
        auto buffer = get_audio_buffer(key);
        if (!buffer || buffer->channels.empty()) return false;
        
        int num_channels = static_cast<int>(buffer->channels.size());
        buffer->sample_rate = sample_rate;
        
        if (num_channels == 1) {
            buffer->channels[0]->push_batch(data, num_frames);
        } else {
            for (size_t frame = 0; frame < num_frames; ++frame) {
                for (int ch = 0; ch < num_channels; ++ch) {
                    float sample = data[frame * num_channels + ch];
                    buffer->channels[ch]->push(sample);
                }
            }
        }
        
        notify_data_ready();
        return true;
    }
    
    bool audio_buffer_write_planar(const std::string& key,
                                   const float* const* channel_data, size_t num_frames,
                                   int sample_rate, int64_t timestamp_ms = 0) {
        auto buffer = get_audio_buffer(key);
        if (!buffer || buffer->channels.empty()) return false;
        
        buffer->sample_rate = sample_rate;
        
        for (size_t ch = 0; ch < buffer->channels.size(); ++ch) {
            for (size_t i = 0; i < num_frames; ++i) {
                buffer->channels[ch]->push(channel_data[ch][i]);
            }
        }
        
        notify_data_ready();
        return true;
    }
    
    bool audio_buffer_read(const std::string& key, AudioData& out, size_t num_samples) {
        auto buffer = get_audio_buffer(key);
        if (!buffer || buffer->channels.empty()) return false;
        
        out.sample_rate = buffer->sample_rate;
        out.num_channels = static_cast<int>(buffer->channels.size());
        out.channels.clear();
        out.channels.resize(buffer->channels.size());
        
        for (size_t ch = 0; ch < buffer->channels.size(); ++ch) {
            out.channels[ch].reserve(num_samples);
            for (size_t i = 0; i < num_samples; ++i) {
                float sample;
                if (!buffer->channels[ch]->pop(sample)) {
                    if (i == 0) return false;
                    break;
                }
                out.channels[ch].push_back(sample);
            }
        }
        
        return !out.empty();
    }
    
    size_t audio_buffer_available(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) return 0;
        
        try {
            auto buffer = std::any_cast<std::shared_ptr<AudioBufferInternal>>(it->second);
            if (buffer->channels.empty()) return 0;
            return buffer->channels[0]->size();
        } catch (...) {
            return 0;
        }
    }
    
    std::shared_ptr<AudioBufferInternal> get_audio_buffer(const std::string& key) {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
            return nullptr;
        }
        try {
            return std::any_cast<std::shared_ptr<AudioBufferInternal>>(it->second);
        } catch (...) {
            return nullptr;
        }
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::any> data_;
    std::vector<Error> errors_;
    State state_;
    ErrorCallback error_callback_;
    CallbackMode callback_mode_ = CallbackMode::Sync;
    detail::AsyncCallbackQueue async_queue_;
    
    mutable std::mutex data_mutex_;
    std::condition_variable data_cv_;
    std::atomic<bool> data_ready_{false};
    std::atomic<bool> data_stopped_{false};
    
    ProcessingStats performance_stats_;
    
public:
    ProcessingStats& performance_stats() { return performance_stats_; }
    const ProcessingStats& performance_stats() const { return performance_stats_; }
    
    void record_operator_time(const std::string& op_id, double time_ms) {
        std::unique_lock lock(mutex_);
        performance_stats_.record_operator_time(op_id, time_ms);
    }
};
}
