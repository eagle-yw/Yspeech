module;

#include <taskflow/taskflow.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>

export module yspeech.pipeline_manager;

import std;
import yspeech.error;
import yspeech.state;
import yspeech.context;
import yspeech.aspect;
import yspeech.op;
import yspeech.log;
import yspeech.pipeline;
import yspeech.pipeline_config;
import yspeech.ring_buffer;

namespace yspeech {

namespace detail {

class PipelineStage {
public:
    PipelineStage(const PipelineStageConfig& config, const PipelineConfig& parent_config)
        : config_(config), parent_config_(parent_config) {}
    
    void build() {
        std::unordered_map<std::string, tf::Task> tasks;
        
        for (const auto& op_config : config_.ops()) {
            const std::string& id = op_config.id;
            const std::string& name = op_config.name;
            
            log_info("Creating operator: {} ({})", id, name);

            try {
                auto op = OperatorFactory::get_instance().create_operator(name);
                
                if (!op_config.params.is_null()) {
                    op.init(op_config.params);
                }
                
                install_global_capabilities(op);
                
                operators_.emplace(id, std::move(op));
            } catch (const std::exception& e) {
                Error err{
                    .source = id,
                    .component = "PipelineStage",
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
        
        for (const auto& op_config : config_.ops()) {
            const std::string& id = op_config.id;
            for (const auto& dep_id : op_config.depends_on) {
                if (tasks.count(dep_id) && tasks.count(id)) {
                    tasks[dep_id].precede(tasks[id]);
                } else {
                    log_warn("Dependency {} not found for {}", dep_id, id);
                }
            }
        }
        
        log_info("PipelineStage '{}' built with {} operators", 
                 config_.id().empty() ? "unnamed" : config_.id(), 
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
        taskflow_.clear();
        error_handling_configs_.clear();
        build_errors_.clear();
    }
    
    const std::string& id() const { return config_.id(); }
    const std::string& name() const { return config_.name(); }
    const StageInputConfig& input() const { return config_.input(); }
    const StageOutputConfig& output() const { return config_.output(); }
    size_t max_concurrency() const { return config_.max_concurrency(); }
    const std::vector<Error>& build_errors() const { return build_errors_; }

private:
    PipelineStageConfig config_;
    const PipelineConfig& parent_config_;
    std::unordered_map<std::string, OperatorIface> operators_;
    tf::Taskflow taskflow_;
    tf::Executor executor_;
    std::atomic<Context*> ctx_{nullptr};
    std::unordered_map<std::string, ErrorHandlingConfig> error_handling_configs_;
    std::vector<Error> build_errors_;

    void install_global_capabilities(OperatorIface& op) {
        for (const auto& cap_config : parent_config_.capabilities()) {
            if (!cap_config.contains("name")) continue;
            
            std::string cap_name = cap_config["name"].get<std::string>();
            
            if (op.has_capability(cap_name)) continue;
            
            nlohmann::json cap_params = cap_config.value("params", nlohmann::json::object());
            cap_params = parent_config_.resolve_capability_params(cap_params);
            op.install(cap_name, cap_params);
        }
    }

    ErrorHandlingConfig get_error_config(const std::string& id) {
        auto it = error_handling_configs_.find(id);
        if (it != error_handling_configs_.end()) {
            return it->second;
        }
        return ErrorHandlingConfig{};
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
        int attempt = 1;
        
        while (attempt <= max_attempts) {
            try {
                op_ref.process(*ctx);
                return;
            } catch (const std::exception& e) {
                log_error("Error in operator {} (attempt {}/{}): {}", id, attempt, max_attempts, e.what());
                
                ctx->record_error(id, e.what(), "Operator", ErrorCode::OperatorProcessFailed, 
                                  ErrorLevel::Error, attempt, false);
                
                if (eh_config.strategy == ErrorStrategy::Skip) {
                    log_warn("Skipping operator {} due to error", id);
                    ctx->state().mark_skipped();
                    return;
                }
                
                if (eh_config.strategy == ErrorStrategy::Retry && attempt < max_attempts) {
                    if (eh_config.retry_delay_ms > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(eh_config.retry_delay_ms));
                    }
                    ++attempt;
                    continue;
                }
                
                throw;
            }
        }
    }
};

}

export class PipelineManager {
public:
    PipelineManager() = default;
    
    ~PipelineManager() {
        stop();
        clear();
    }

    PipelineManager(const PipelineManager&) = delete;
    PipelineManager& operator=(const PipelineManager&) = delete;
    PipelineManager(PipelineManager&&) noexcept = delete;
    PipelineManager& operator=(PipelineManager&&) noexcept = delete;

    void build(const std::string& config_path) {
        auto config = PipelineConfig::from_file(config_path);
        build(config);
    }

    void build(const PipelineConfig& config) {
        config_ = config;
        
        for (size_t i = 0; i < config.stage_count(); ++i) {
            auto stage = std::make_unique<detail::PipelineStage>(config.stage(i), config);
            stage->build();
            stages_.push_back(std::move(stage));
        }
        
        init_buffers();
        
        log_info("PipelineManager built with {} stage(s)", stages_.size());
    }
    
    void run(Context& ctx) {
        if (stages_.empty()) {
            log_warn("PipelineManager has no stages to run");
            return;
        }
        
        if (config_.is_single_stage()) {
            run_single_stage(ctx);
        } else {
            run_multi_stage(ctx);
        }
    }
    
    void start_async(Context& ctx) {
        if (running_) return;
        
        running_ = true;
        runner_thread_ = std::thread([this, &ctx]() {
            run(ctx);
        });
    }
    
    void stop() {
        running_ = false;
        
        if (ctx_) {
            ctx_->stop_data();
        }
        
        if (runner_thread_.joinable()) {
            runner_thread_.join();
        }
    }
    
    void clear() {
        for (auto& stage : stages_) {
            stage->clear();
        }
        stages_.clear();
    }
    
    bool is_running() const { return running_; }
    bool is_single_stage() const { return config_.is_single_stage(); }
    size_t stage_count() const { return stages_.size(); }
    
    const detail::PipelineStage* stage(size_t index) const {
        return index < stages_.size() ? stages_[index].get() : nullptr;
    }

private:
    PipelineConfig config_;
    std::vector<std::unique_ptr<detail::PipelineStage>> stages_;
    std::atomic<bool> running_{false};
    std::thread runner_thread_;
    Context* ctx_ = nullptr;

    void init_buffers() {
    }

    void run_single_stage(Context& ctx) {
        ctx_ = &ctx;
        stages_[0]->run(ctx);
        ctx_ = nullptr;
    }
    
    void run_multi_stage(Context& ctx) {
        ctx_ = &ctx;
        
        std::vector<std::thread> stage_threads;
        stage_threads.reserve(stages_.size());
        
        for (size_t i = 0; i < stages_.size(); ++i) {
            stage_threads.emplace_back([this, i, &ctx]() {
                stages_[i]->run(ctx);
            });
        }
        
        for (auto& t : stage_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        
        ctx_ = nullptr;
    }
};

}
