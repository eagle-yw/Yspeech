module;

#include <nlohmann/json.hpp>

export module yspeech.stream_controller;

import std;
import yspeech.context;
import yspeech.pipeline_manager;
import yspeech.log;
import yspeech.types;

namespace yspeech {

export template<typename T>
class StreamController {
public:
    using DataSource = std::function<bool(std::vector<T>&)>;
    
    StreamController() = default;
    
    ~StreamController() {
        stop();
    }
    
    StreamController(const StreamController&) = delete;
    StreamController& operator=(const StreamController&) = delete;
    StreamController(StreamController&&) noexcept = delete;
    StreamController& operator=(StreamController&&) noexcept = delete;
    
    void set_data_source(DataSource source) {
        data_source_ = std::move(source);
    }
    
    void set_buffer_key(const std::string& key) {
        buffer_key_ = key;
    }
    
    void set_eof_flag(const std::string& flag) {
        eof_flag_ = flag;
    }
    
    void set_manager(PipelineManager* manager) {
        manager_ = manager;
    }

    void start(Context& ctx) {
        if (running_) return;
        
        running_ = true;
        ctx_ = &ctx;
        
        if (manager_) {
            processing_thread_ = std::thread([this, &ctx]() {
                run_processing_loop();
            });
        }
    }
    
    void stop() {
        if (!running_) return;
        
        running_ = false;
        
        if (ctx_) {
            ctx_->stop_data();
        }
        
        if (processing_thread_.joinable()) {
            processing_thread_.join();
        }
    }
    
    bool is_running() const { return running_; }
    
    size_t chunks_pushed() const { return chunks_pushed_.load(); }
    
    bool eof_reached() const { return eof_reached_.load(); }

private:
    DataSource data_source_;
    std::string buffer_key_ = "input_buffer";
    std::string eof_flag_ = "global_eof";
    PipelineManager* manager_ = nullptr;
    Context* ctx_ = nullptr;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> eof_reached_{false};
    std::atomic<size_t> chunks_pushed_{0};
    
    std::thread processing_thread_;

    void run_processing_loop() {
        if (!data_source_ || !ctx_ || !manager_) {
            log_error("StreamController: data source, context or manager not set");
            return;
        }
        
        log_debug("StreamController started, buffer: {}", buffer_key_);
        
        while (running_) {
            std::vector<T> chunk;
            bool has_data = data_source_(chunk);
            
            if (!has_data) {
                log_debug("StreamController: data source exhausted");
                eof_reached_ = true;
                break;
            }
            
            chunks_pushed_++;
            
            manager_->run(*ctx_);
        }
        
        if (ctx_) {
            ctx_->set(eof_flag_, true);
            ctx_->stop_data();
        }
        
        log_debug("StreamController stopped, pushed {} chunks", chunks_pushed_.load());
    }
};

export using AudioStreamController = StreamController<std::vector<Byte>>;

}
