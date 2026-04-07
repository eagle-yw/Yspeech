module;

#include <taskflow/taskflow.hpp>
#include <nlohmann/json.hpp>

export module yspeech.pipeline_manager;

import std;
import yspeech.error;
import yspeech.state;
import yspeech.context;
import yspeech.aspect;
import yspeech.aspect.timer;
import yspeech.op;
import yspeech.log;
import yspeech.pipeline_config;
import yspeech.stream_store;

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

class PipelineStage {
public:
    PipelineStage(const PipelineStageConfig& config,
                  const PipelineConfig& parent_config,
                  std::vector<AspectIface> shared_aspects = {})
        : config_(config),
          parent_config_(parent_config),
          shared_aspects_(std::move(shared_aspects)) {}
    
    void build() {
        init_aspects();
        
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
        
        log_debug("PipelineStage '{}' built with {} operators", 
                 config_.id().empty() ? "unnamed" : config_.id(), 
                 operators_.size());
    }
    
    void run(Context& ctx) {
        throw std::runtime_error("Batch pipeline execution has been removed; use run_stream()");
    }

    void run_stream(Context& ctx, StreamStore& store, bool flush) {
        execute(ctx, &store, flush);
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
    void dump_graph(std::ostream& os) const { taskflow_.dump(os); }

private:
    PipelineStageConfig config_;
    const PipelineConfig& parent_config_;
    std::unordered_map<std::string, OperatorIface> operators_;
    tf::Taskflow taskflow_;
    tf::Executor executor_;
    std::atomic<Context*> ctx_{nullptr};
    std::atomic<StreamStore*> stream_store_{nullptr};
    std::atomic<bool> flush_mode_{false};
    std::unordered_map<std::string, ErrorHandlingConfig> error_handling_configs_;
    std::vector<Error> build_errors_;
    std::vector<AspectIface> aspects_;
    std::vector<AspectIface> shared_aspects_;

    void execute(Context& ctx, StreamStore* store, bool flush) {
        for (const auto& err : build_errors_) {
            ctx.record_error(err);
        }

        ctx_.store(&ctx, std::memory_order_release);
        stream_store_.store(store, std::memory_order_release);
        flush_mode_.store(flush, std::memory_order_release);
        executor_.run(taskflow_).wait();
        flush_mode_.store(false, std::memory_order_release);
        stream_store_.store(nullptr, std::memory_order_release);
        ctx_.store(nullptr, std::memory_order_release);
    }

    void init_aspects() {
        aspects_.emplace_back(TimerAspect{});
        for (const auto& aspect : shared_aspects_) {
            aspects_.push_back(aspect);
        }
    }

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
        StreamStore* store = stream_store_.load(std::memory_order_acquire);
        const bool flush = flush_mode_.load(std::memory_order_acquire);
        
        auto it = operators_.find(id);
        if (it == operators_.end()) return;
        
        OperatorIface& op_ref = it->second;
        auto eh_config = get_error_config(id);
        int max_attempts = (eh_config.strategy == ErrorStrategy::Retry) 
            ? eh_config.max_retries + 1 : 1;
        int attempt = 1;
        
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
        
        while (attempt <= max_attempts) {
            try {
                if (store) {
                    if (flush) {
                        op_ref.flush_stream(*ctx, *store);
                    } else if (op_ref.ready_stream(*ctx, *store)) {
                        op_ref.process_stream(*ctx, *store);
                    }
                } else {
                    throw std::runtime_error(
                        std::format("Operator {} requires streaming execution", id));
                }
                call_after_methods();
                return;
            } catch (const std::exception& e) {
                log_error("Error in operator {} (attempt {}/{}): {}", id, attempt, max_attempts, e.what());
                
                ctx->record_error(id, e.what(), "Operator", ErrorCode::OperatorProcessFailed, 
                                  ErrorLevel::Error, attempt, false);
                
                call_after_methods();
                
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

    template<Aspect T>
    void add_aspect(T&& aspect) {
        aspects_.emplace_back(std::forward<T>(aspect));
    }

    void build(const std::string& config_path) {
        auto config = PipelineConfig::from_file(config_path);
        build(config);
    }

    void build(const PipelineConfig& config) {
        clear();
        config_ = config;
        
        for (size_t i = 0; i < config.stage_count(); ++i) {
            auto stage = std::make_unique<detail::PipelineStage>(config.stage(i), config, aspects_);
            stage->build();
            stages_.push_back(std::move(stage));
        }
        
        init_buffers();
        
        log_debug("PipelineManager built with {} stage(s)", stages_.size());
    }
    
    void run(Context& ctx) {
        if (stages_.empty()) {
            log_warn("PipelineManager has no stages to run");
            return;
        }

        throw std::runtime_error("Batch pipeline execution has been removed; use run_stream()");
    }

    void run_stream(Context& ctx, StreamStore& store, bool flush = false) {
        if (stages_.empty()) {
            log_warn("PipelineManager has no stages to run");
            return;
        }

        if (config_.is_single_stage()) {
            run_stream_single_stage(ctx, store, flush);
        } else {
            run_stream_multi_stage(ctx, store, flush);
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

    std::vector<Error> build_errors() const {
        std::vector<Error> errors;
        for (const auto& stage : stages_) {
            const auto& stage_errors = stage->build_errors();
            errors.insert(errors.end(), stage_errors.begin(), stage_errors.end());
        }
        return errors;
    }

    bool has_build_errors() const {
        return std::ranges::any_of(stages_, [](const auto& stage) {
            return !stage->build_errors().empty();
        });
    }

    void dump_graph(std::ostream& os) const {
        for (std::size_t i = 0; i < stages_.size(); ++i) {
            if (i > 0) {
                os << "\n";
            }
            stages_[i]->dump_graph(os);
        }
    }

private:
    PipelineConfig config_;
    std::vector<std::unique_ptr<detail::PipelineStage>> stages_;
    std::vector<AspectIface> aspects_;
    std::atomic<bool> running_{false};
    std::thread runner_thread_;
    Context* ctx_ = nullptr;

    void init_buffers() {
    }

    template <typename Runner>
    void run_stage_threads(Context& ctx, Runner&& runner) {
        ctx_ = &ctx;

        std::vector<std::thread> stage_threads;
        stage_threads.reserve(stages_.size());

        for (size_t i = 0; i < stages_.size(); ++i) {
            stage_threads.emplace_back([this, i, &ctx, &runner]() {
                runner(*stages_[i], ctx);
            });
        }

        for (auto& t : stage_threads) {
            if (t.joinable()) {
                t.join();
            }
        }

        ctx_ = nullptr;
    }

    void run_single_stage(Context& ctx) {
        ctx_ = &ctx;
        stages_[0]->run(ctx);
        ctx_ = nullptr;
    }

    void run_stream_single_stage(Context& ctx, StreamStore& store, bool flush) {
        ctx_ = &ctx;
        stages_[0]->run_stream(ctx, store, flush);
        ctx_ = nullptr;
    }

    void run_multi_stage(Context& ctx) {
        run_stage_threads(ctx, [](detail::PipelineStage& stage, Context& stage_ctx) {
            stage.run(stage_ctx);
        });
    }

    void run_stream_multi_stage(Context& ctx, StreamStore& store, bool flush) {
        run_stage_threads(ctx, [&store, flush](detail::PipelineStage& stage, Context& stage_ctx) {
            stage.run_stream(stage_ctx, store, flush);
        });
    }
};

export using Pipeline [[deprecated("Use PipelineManager instead")]] = PipelineManager;

}
