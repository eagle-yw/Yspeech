module;

#include <taskflow/taskflow.hpp>
#include <nlohmann/json.hpp>

export module yspeech.pipeline;

import std;
import yspeech.error;
import yspeech.state;
import yspeech.context;
import yspeech.aspect;
import yspeech.aspect.timer;
import yspeech.op;
import yspeech.log;
import yspeech.pipeline_config;

namespace yspeech {

export enum class ErrorStrategy {
    Fail,
    Skip,
    Retry
};

export struct ErrorHandlingConfig {
    ErrorStrategy strategy = ErrorStrategy::Fail;
    int max_retries = 3;
    int retry_delay_ms = 100;
};

namespace detail {

class DelayedTaskQueue {
public:
    using Task = std::function<void()>;
    
    struct DelayedTask {
        std::chrono::steady_clock::time_point execute_time;
        Task task;
        
        bool operator>(const DelayedTask& other) const {
            return execute_time > other.execute_time;
        }
    };
    
    void push(Task task, int delay_ms) {
        auto execute_time = std::chrono::steady_clock::now() + 
                           std::chrono::milliseconds(delay_ms);
        {
            std::unique_lock lock(mutex_);
            queue_.push({execute_time, std::move(task)});
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
                
                if (!running_) break;
                
                auto now = std::chrono::steady_clock::now();
                
                while (!queue_.empty() && queue_.top().execute_time <= now) {
                    auto task = queue_.top().task;
                    queue_.pop();
                    lock.unlock();
                    task();
                    lock.lock();
                }
                
                if (!queue_.empty()) {
                    auto sleep_duration = queue_.top().execute_time - now;
                    cv_.wait_for(lock, sleep_duration);
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
    
    ~DelayedTaskQueue() {
        stop();
    }

private:
    std::priority_queue<DelayedTask, std::vector<DelayedTask>, std::greater<>> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}

export class Pipeline {
public:
    Pipeline() {
        delayed_queue_.start();
        add_aspect(TimerAspect{});
    }
    
    ~Pipeline() {
        delayed_queue_.stop();
        clear();
    }

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) noexcept = delete;
    Pipeline& operator=(Pipeline&&) noexcept = delete;

    template<Aspect T>
    void add_aspect(T&& aspect) {
        aspects_.emplace_back(std::forward<T>(aspect));
    }

    void set_error_handling(const ErrorHandlingConfig& config) {
        error_handling_ = config;
    }

    void build(const std::string& config_path) {
        try {
            auto config = PipelineConfig::from_file(config_path);
            build(config);
        } catch (const std::exception& e) {
            Error err{
                .source = config_path,
                .component = "Pipeline",
                .message = std::string("Failed to build pipeline from config: ") + e.what(),
                .code = ErrorCode::InvalidConfig,
                .level = ErrorLevel::Error,
                .attempt = 0,
                .recovered = false,
                .timestamp = std::chrono::system_clock::now(),
                .metadata = nlohmann::json{{"config_path", config_path}}
            };
            build_errors_.push_back(err);
            log_error("Failed to build pipeline from config: {}", e.what());
            throw;
        }
    }

    void build(const PipelineConfig& config) {
        if (!config.is_single_stage()) {
            throw std::runtime_error("Pipeline class only supports single-stage configs. Use PipelineManager for multi-stage.");
        }
        
        const auto& stage = config.stage(0);
        build_stage(stage, config);
        
        log_info("Pipeline '{}' v{} built with {} operators", 
                 config.name().empty() ? "unnamed" : config.name(), 
                 config.version().empty() ? "0.0" : config.version(),
                 operators_.size());
    }
    
    void run(Context& ctx) {
        for (const auto& err : build_errors_) {
            ctx.record_error(err);
        }
        
        ctx_.store(&ctx, std::memory_order_release);
        executor_.run(taskflow_).wait();
        ctx_.store(nullptr, std::memory_order_release);
    }
    
    void clear() {
        for (auto& [id, op] : operators_) {
            op.deinit();
        }
        operators_.clear();
        aspects_.clear();
        taskflow_.clear();
        error_handling_configs_.clear();
        build_errors_.clear();
    }
    
    void dump_graph(std::ostream& os) const {
        taskflow_.dump(os);
    }
    
    const std::vector<Error>& build_errors() const {
        return build_errors_;
    }
    
    bool has_build_errors() const {
        return !build_errors_.empty();
    }

private:
    std::unordered_map<std::string, OperatorIface> operators_;
    std::vector<AspectIface> aspects_;
    tf::Taskflow taskflow_;
    tf::Executor executor_;
    std::atomic<Context*> ctx_{nullptr};
    ErrorHandlingConfig error_handling_;
    std::unordered_map<std::string, ErrorHandlingConfig> error_handling_configs_;
    std::vector<Error> build_errors_;
    detail::DelayedTaskQueue delayed_queue_;

    void build_stage(const PipelineStageConfig& stage, const PipelineConfig& config) {
        std::unordered_map<std::string, tf::Task> tasks;
        
        for (const auto& op_config : stage.ops()) {
            const std::string& id = op_config.id;
            const std::string& name = op_config.name;
            
            log_info("Creating operator: {} ({})", id, name);

            try {
                auto op = OperatorFactory::get_instance().create_operator(name);
                
                if (!op_config.params.is_null()) {
                    op.init(op_config.params);
                }
                
                install_global_capabilities(op, config);
                
                operators_.emplace(id, std::move(op));
            } catch (const std::exception& e) {
                Error err{
                    .source = id,
                    .component = "Operator",
                    .message = std::string("Failed to create operator: ") + e.what(),
                    .code = ErrorCode::OperatorInitFailed,
                    .level = ErrorLevel::Error,
                    .attempt = 0,
                    .recovered = false,
                    .timestamp = std::chrono::system_clock::now(),
                    .metadata = nlohmann::json{{"operator_name", name}, {"operator_id", id}}
                };
                build_errors_.push_back(err);
                log_error("Failed to create operator {}: {}", id, e.what());
                throw;
            }
            
            if (!op_config.error_handling.is_null()) {
                const auto& eh_config = op_config.error_handling;
                ErrorHandlingConfig eh;
                if (eh_config.contains("strategy")) {
                    std::string strategy = eh_config["strategy"];
                    if (strategy == "skip") {
                        eh.strategy = ErrorStrategy::Skip;
                    } else if (strategy == "retry") {
                        eh.strategy = ErrorStrategy::Retry;
                    } else {
                        eh.strategy = ErrorStrategy::Fail;
                    }
                }
                if (eh_config.contains("max_retries")) {
                    eh.max_retries = eh_config["max_retries"].get<int>();
                }
                if (eh_config.contains("retry_delay_ms")) {
                    eh.retry_delay_ms = eh_config["retry_delay_ms"].get<int>();
                }
                error_handling_configs_[id] = eh;
            }
        }

        for (const auto& [id, op] : operators_) {
            std::string task_id = id;
            tasks[id] = taskflow_.emplace([this, task_id](){ 
                run_task(task_id);
            }).name(id);
        }
        
        for (const auto& op_config : stage.ops()) {
            const std::string& id = op_config.id;
            for (const auto& dep_id : op_config.depends_on) {
                if (tasks.count(dep_id) && tasks.count(id)) {
                    tasks[dep_id].precede(tasks[id]);
                } else {
                    log_warn("Dependency {} not found for {}", dep_id, id);
                }
            }
        }
    }

    void install_global_capabilities(OperatorIface& op, const PipelineConfig& config) {
        for (const auto& cap_config : config.capabilities()) {
            if (!cap_config.contains("name")) continue;
            
            std::string cap_name = cap_config["name"].get<std::string>();
            
            if (op.has_capability(cap_name)) continue;
            
            nlohmann::json cap_params = cap_config.value("params", nlohmann::json::object());
            cap_params = config.resolve_capability_params(cap_params);
            op.install(cap_name, cap_params);
        }
    }

    ErrorHandlingConfig get_error_config(const std::string& id) {
        auto it = error_handling_configs_.find(id);
        if (it != error_handling_configs_.end()) {
            return it->second;
        }
        return error_handling_;
    }

    void run_task(const std::string& id) {
        Context* ctx = ctx_.load(std::memory_order_acquire);
        if (!ctx) return;
        
        auto it = operators_.find(id);
        if (it == operators_.end()) return;
        
        OperatorIface& op_ref = it->second;
        auto eh_config = get_error_config(id);
        int max_attempts = (eh_config.strategy == ErrorStrategy::Retry) 
            ? eh_config.max_retries + 1 : 1;
        
        std::vector<std::any> aspect_payloads(aspects_.size());
        for (size_t i = 0; i < aspects_.size(); ++i) {
            aspect_payloads[i] = aspects_[i].before(*ctx, id);
        }

        auto call_after_methods = [&]() {
            for (size_t i = 0; i < aspects_.size(); ++i) {
                size_t idx = aspects_.size() - 1 - i;
                aspects_[idx].after(*ctx, id, std::move(aspect_payloads[idx]));
            }
        };

        try {
            op_ref.process(*ctx);
            call_after_methods();
        } catch (const std::exception& e) {
            log_error("Error in operator {}: {}", id, e.what());
            
            ctx->record_error(id, e.what(), "Operator", ErrorCode::OperatorProcessFailed, 
                              ErrorLevel::Error, 1, false);
            
            call_after_methods();
            
            handle_error(id, 1, max_attempts, eh_config);
        } catch (...) {
            log_error("Unknown error in operator {}", id);
            
            ctx->record_error(id, "Unknown error", "Operator", ErrorCode::OperatorProcessFailed, 
                              ErrorLevel::Error, 1, false);
            
            call_after_methods();
            
            handle_error(id, 1, max_attempts, eh_config);
        }
    }
    
    void handle_error(const std::string& id, int attempt, int max_attempts, 
                      const ErrorHandlingConfig& eh_config) {
        Context* ctx = ctx_.load(std::memory_order_acquire);
        
        if (eh_config.strategy == ErrorStrategy::Skip) {
            log_warn("Skipping operator {} due to error", id);
            if (ctx) ctx->state().mark_skipped();
            return;
        }
        
        if (eh_config.strategy == ErrorStrategy::Retry && attempt < max_attempts) {
            log_info("Scheduling retry for operator {} in {}ms", id, eh_config.retry_delay_ms);
            
            delayed_queue_.push([this, id, attempt]() {
                run_task(id);
            }, eh_config.retry_delay_ms);
            return;
        }
        
        throw std::runtime_error("Operator " + id + " failed after " + std::to_string(attempt) + " attempts");
    }
};

}
