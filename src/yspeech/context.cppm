module;

#include <nlohmann/json.hpp>

export module yspeech.context;

import std;
import yspeech.error;
import yspeech.state;
import yspeech.types;
import yspeech.frame_ring;
import yspeech.ring_buffer;
import yspeech.data_keys;

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

struct AudioFrameStreamInternal {
    std::shared_ptr<FrameRing> ring;
    std::unordered_map<std::string, std::shared_ptr<FrameReader>> readers;
};

export class Context {
public:
    using ErrorCallback = std::function<void(const Error&)>;
    
    enum class CallbackMode {
        Sync,
        Async
    };
    
    enum class PatternMatch {
        Prefix,
        Suffix,
        Contains,
        Exact
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
        std::any boxed_value(std::forward<T>(value));
        std::unique_lock lock(mutex_);
        data_[key] = std::move(boxed_value);
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
    
    template <typename T>
    bool validate(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
            return false;
        }
        return it->second.type() == typeid(T);
    }
    
    template <typename T, typename Validator>
    bool validate_with(const std::string& key, Validator&& validator) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
            return false;
        }
        if (it->second.type() != typeid(T)) {
            return false;
        }
        try {
            const T& value = std::any_cast<const T&>(it->second);
            return validator(value);
        } catch (...) {
            return false;
        }
    }
    
    template <typename T>
    T get_or_default(const std::string& key, const T& default_value) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
            return default_value;
        }
        try {
            return std::any_cast<T>(it->second);
        } catch (const std::bad_any_cast&) {
            return default_value;
        }
    }
    
    template <typename T>
    bool try_get(const std::string& key, T& out_value) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
            return false;
        }
        try {
            out_value = std::any_cast<T>(it->second);
            return true;
        } catch (const std::bad_any_cast&) {
            return false;
        }
    }
    
    void clear_by_pattern(const std::string& pattern, PatternMatch mode = PatternMatch::Prefix) {
        std::unique_lock lock(mutex_);
        for (auto it = data_.begin(); it != data_.end(); ) {
            bool match = false;
            switch (mode) {
                case PatternMatch::Prefix:
                    match = it->first.starts_with(pattern);
                    break;
                case PatternMatch::Suffix:
                    match = it->first.ends_with(pattern);
                    break;
                case PatternMatch::Contains:
                    match = it->first.find(pattern) != std::string::npos;
                    break;
                case PatternMatch::Exact:
                    match = it->first == pattern;
                    break;
            }
            if (match) {
                it = data_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    void clear_accumulating_keys() {
        std::unique_lock lock(mutex_);
        for (auto it = data_.begin(); it != data_.end(); ) {
            if (DataKeys::is_accumulating_key(it->first)) {
                it = data_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    std::vector<std::string> list_keys() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> keys;
        keys.reserve(data_.size());
        for (const auto& [key, _] : data_) {
            keys.push_back(key);
        }
        return keys;
    }
    
    std::size_t data_size() const {
        std::shared_lock lock(mutex_);
        return data_.size();
    }
    
    template <typename T>
    void set_typed(const TypedKey<T>& key, T&& value) {
        std::unique_lock lock(mutex_);
        data_[key.name()] = std::forward<T>(value);
    }
    
    template <typename T>
    T get_typed(const TypedKey<T>& key) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key.name());
        if (it == data_.end()) {
            return key.default_value();
        }
        try {
            return std::any_cast<T>(it->second);
        } catch (const std::bad_any_cast&) {
            return key.default_value();
        }
    }
    
    template <typename T>
    bool validate_typed(const TypedKey<T>& key) const {
        return validate<T>(key.name());
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

    void init_audio_frame_queue(const std::string& key, std::size_t capacity_frames = 6000) {
        std::unique_lock lock(mutex_);
        auto stream = std::make_shared<AudioFrameStreamInternal>();
        stream->ring = std::make_shared<FrameRing>(capacity_frames);
        data_[key] = stream;
    }

    bool push_audio_frame(const std::string& key, AudioFramePtr frame) {
        if (!frame) {
            return false;
        }

        auto stream = get_audio_frame_stream(key);
        if (!stream || !stream->ring) {
            return false;
        }

        const bool pushed = stream->ring->push(std::move(frame));
        if (!pushed) {
            return false;
        }
        notify_data_ready();
        return true;
    }

    void register_audio_frame_reader(const std::string& key,
                                     const std::string& reader_key,
                                     std::uint64_t start_seq = 0) {
        auto stream = get_audio_frame_stream(key);
        if (!stream || !stream->ring) {
            return;
        }

        std::unique_lock lock(mutex_);
        stream->readers[reader_key] = std::make_shared<FrameReader>(stream->ring, reader_key, start_seq);
    }

    FrameReadResult read_audio_frame(const std::string& key,
                                     std::string_view reader_key) {
        auto reader = get_audio_frame_reader(key, std::string(reader_key));
        if (!reader) {
            return {};
        }

        return reader->next();
    }

    void seek_audio_frame_reader_to_oldest(const std::string& key,
                                           std::string_view reader_key) {
        auto reader = get_audio_frame_reader(key, std::string(reader_key));
        if (!reader) {
            return;
        }
        reader->seek_to_oldest();
    }

    void reset_audio_frame_reader(const std::string& key,
                                  std::string_view reader_key,
                                  std::uint64_t seq = 0) {
        auto reader = get_audio_frame_reader(key, std::string(reader_key));
        if (!reader) {
            return;
        }
        reader->reset(seq);
    }

    std::size_t audio_frame_queue_size(const std::string& key) const {
        auto stream = get_audio_frame_stream(key);
        if (!stream || !stream->ring) {
            return 0;
        }
        return stream->ring->size();
    }
    
    std::shared_ptr<AudioFrameStreamInternal> get_audio_frame_stream(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
            return nullptr;
        }
        try {
            return std::any_cast<std::shared_ptr<AudioFrameStreamInternal>>(it->second);
        } catch (...) {
            return nullptr;
        }
    }

    std::shared_ptr<FrameReader> get_audio_frame_reader(const std::string& key,
                                                        const std::string& reader_key) {
        auto stream = get_audio_frame_stream(key);
        if (!stream || !stream->ring) {
            return nullptr;
        }

        {
            std::shared_lock lock(mutex_);
            auto it = stream->readers.find(reader_key);
            if (it != stream->readers.end()) {
                return it->second;
            }
        }

        std::unique_lock lock(mutex_);
        auto& reader = stream->readers[reader_key];
        if (!reader) {
            reader = std::make_shared<FrameReader>(stream->ring, reader_key, stream->ring->oldest_seq());
        }
        return reader;
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

    void record_operator_effective_call(const std::string& op_id) {
        std::unique_lock lock(mutex_);
        performance_stats_.record_operator_effective_call(op_id);
    }

    void record_operator_effective_sample(const std::string& op_id, double time_ms) {
        std::unique_lock lock(mutex_);
        performance_stats_.record_operator_effective_sample(op_id, time_ms);
    }
};
}
