module;

#include <taskflow/algorithm/pipeline.hpp>

export module yspeech.runtime.pipeline_executor;

import std;
import yspeech.log;
import yspeech.runtime.pipeline_builder;
import yspeech.runtime.pipeline_recipe;
import yspeech.runtime.token;
import yspeech.runtime.runtime_context;
import yspeech.runtime.segment_registry;

namespace yspeech {

export class PipelineExecutor {
public:
    using StageCallback = std::function<void(PipelineToken&, RuntimeContext&, SegmentRegistry&)>;
    using CompletionCallback = std::function<void(const PipelineToken&, RuntimeContext&, SegmentRegistry&)>;

    PipelineExecutor();
    ~PipelineExecutor();

    void configure(PipelineBuilderConfig config, RuntimeContext& runtime, SegmentRegistry& registry);
    void set_stage_callback(PipelineStageRole role, StageCallback callback);
    void set_vad_stage(StageCallback callback);
    void set_feature_stage(StageCallback callback);
    void set_asr_stage(StageCallback callback);
    void set_event_stage(StageCallback callback);
    void set_completion_callback(CompletionCallback callback);

    void start();
    void push(PipelineToken token);
    void finish();
    void stop();
    void wait();

    bool is_running() const;
    auto pending_tokens() const -> std::size_t;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class PipelineExecutor::Impl {
public:
    using PipeType = tf::Pipe<std::function<void(tf::Pipeflow&)>>;
    using PipelineType = tf::Pipeline<PipeType, PipeType, PipeType, PipeType, PipeType>;

    void configure(PipelineBuilderConfig config, RuntimeContext& runtime, SegmentRegistry& registry) {
        std::scoped_lock lock(state_mutex);
        if (running) {
            throw std::runtime_error("cannot reconfigure pipeline executor while running");
        }
        builder_config = std::move(config);
        runtime_context = &runtime;
        segment_registry = &registry;
    }

    void set_stage_callback(PipelineStageRole role, PipelineExecutor::StageCallback callback) {
        std::scoped_lock lock(state_mutex);
        stage_callbacks[role] = std::move(callback);
    }

    void set_vad_stage(PipelineExecutor::StageCallback callback) {
        set_stage_callback(PipelineStageRole::Vad, std::move(callback));
    }

    void set_feature_stage(PipelineExecutor::StageCallback callback) {
        set_stage_callback(PipelineStageRole::Feature, std::move(callback));
    }

    void set_asr_stage(PipelineExecutor::StageCallback callback) {
        set_stage_callback(PipelineStageRole::Asr, std::move(callback));
    }

    void set_event_stage(PipelineExecutor::StageCallback callback) {
        set_stage_callback(PipelineStageRole::Event, std::move(callback));
    }

    void set_completion_callback(PipelineExecutor::CompletionCallback callback) {
        std::scoped_lock lock(state_mutex);
        completion_callback = std::move(callback);
    }

    void start() {
        std::scoped_lock lock(state_mutex);
        if (running) {
            return;
        }
        if (!runtime_context || !segment_registry) {
            throw std::runtime_error("pipeline executor is not configured");
        }

        stopping = false;
        finish_requested = false;
        line_tokens.assign(builder_config.num_lines, std::nullopt);
        taskflow = std::make_unique<tf::Taskflow>(builder_config.name);
        pipeline = std::make_unique<PipelineType>(
            builder_config.num_lines,
            PipeType{tf::PipeType::SERIAL, [this](tf::Pipeflow& pf) { source_pipe(pf); }},
            PipeType{tf::PipeType::SERIAL, [this](tf::Pipeflow& pf) { vad_pipe(pf); }},
            PipeType{tf::PipeType::PARALLEL, [this](tf::Pipeflow& pf) { feature_pipe(pf); }},
            PipeType{tf::PipeType::PARALLEL, [this](tf::Pipeflow& pf) { asr_pipe(pf); }},
            PipeType{tf::PipeType::SERIAL, [this](tf::Pipeflow& pf) { event_pipe(pf); }}
        );
        taskflow->composed_of(*pipeline).name(builder_config.name);
        run_future = executor.run(*taskflow);
        running = true;
        log_info("Pipeline executor started with {} lines", builder_config.num_lines);
    }

    void push(PipelineToken token) {
        {
            std::unique_lock lock(queue_mutex);
            queue_cv.wait(lock, [this]() {
                return stopping.load(std::memory_order_acquire)
                    || token_queue.size() < builder_config.queue_capacity;
            });
            if (stopping.load(std::memory_order_acquire)) {
                return;
            }
            token_queue.push_back(std::move(token));
        }
        queue_cv.notify_all();
    }

    void finish() {
        finish_requested.store(true, std::memory_order_release);
        queue_cv.notify_all();
    }

    void stop() {
        {
            std::scoped_lock lock(state_mutex);
            if (!running) {
                return;
            }
            stopping.store(true, std::memory_order_release);
            finish_requested.store(true, std::memory_order_release);
        }

        queue_cv.notify_all();
        wait();
    }

    void wait() {
        std::optional<std::future<void>> future;
        {
            std::scoped_lock lock(state_mutex);
            if (!run_future.has_value()) {
                running = false;
                return;
            }
            future = std::move(run_future);
        }

        future->wait();

        std::scoped_lock lock(state_mutex);
        taskflow.reset();
        pipeline.reset();
        line_tokens.clear();
        running = false;
        log_info("Pipeline executor stopped");
    }

    bool is_running() const {
        return running.load(std::memory_order_acquire);
    }

    auto pending_tokens() const -> std::size_t {
        std::lock_guard lock(queue_mutex);
        return token_queue.size();
    }

private:
    void source_pipe(tf::Pipeflow& pf) {
        std::unique_lock lock(queue_mutex);
        queue_cv.wait(lock, [this]() {
            return stopping.load(std::memory_order_acquire)
                || finish_requested.load(std::memory_order_acquire)
                || !token_queue.empty();
        });

        if (token_queue.empty()) {
            line_tokens[pf.line()].reset();
            pf.stop();
            return;
        }

        line_tokens[pf.line()] = std::move(token_queue.front());
        token_queue.pop_front();
        queue_cv.notify_all();
        if (line_tokens[pf.line()].has_value()) {
            line_tokens[pf.line()]->line_id = pf.line();
        }
    }

    void vad_pipe(tf::Pipeflow& pf) {
        process_role(pf, PipelineStageRole::Vad);
    }

    void feature_pipe(tf::Pipeflow& pf) {
        process_role(pf, PipelineStageRole::Feature);
    }

    void asr_pipe(tf::Pipeflow& pf) {
        process_role(pf, PipelineStageRole::Asr);
    }

    void event_pipe(tf::Pipeflow& pf) {
        process_role(pf, PipelineStageRole::Event);
        auto& slot = line_tokens[pf.line()];
        if (slot && runtime_context && segment_registry) {
            auto completion = snapshot_completion_callback();
            if (completion) {
                completion(*slot, *runtime_context, *segment_registry);
            }
        }
        slot.reset();
    }

    void process_role(tf::Pipeflow& pf, PipelineStageRole role) {
        if (!builder_config.recipe.stages.empty() && !builder_config.recipe.has_role(role) &&
            role != PipelineStageRole::Event) {
            return;
        }

        auto& slot = line_tokens[pf.line()];
        if (!slot || !runtime_context || !segment_registry) {
            return;
        }

        auto callback = snapshot_callback(role);
        if (callback) {
            callback(*slot, *runtime_context, *segment_registry);
        }
    }

    auto snapshot_callback(PipelineStageRole role) -> PipelineExecutor::StageCallback {
        std::scoped_lock lock(state_mutex);
        if (auto it = stage_callbacks.find(role); it != stage_callbacks.end()) {
            return it->second;
        }
        return {};
    }

    auto snapshot_completion_callback() -> PipelineExecutor::CompletionCallback {
        std::scoped_lock lock(state_mutex);
        return completion_callback;
    }

    mutable std::mutex state_mutex;
    mutable std::mutex queue_mutex;
    std::condition_variable queue_cv;

    PipelineBuilderConfig builder_config;
    RuntimeContext* runtime_context = nullptr;
    SegmentRegistry* segment_registry = nullptr;

    std::deque<PipelineToken> token_queue;
    std::vector<std::optional<PipelineToken>> line_tokens;

    std::unordered_map<PipelineStageRole, PipelineExecutor::StageCallback> stage_callbacks;
    PipelineExecutor::CompletionCallback completion_callback;

    std::unique_ptr<tf::Taskflow> taskflow;
    std::unique_ptr<PipelineType> pipeline;
    tf::Executor executor;
    std::optional<std::future<void>> run_future;

    std::atomic<bool> running{false};
    std::atomic<bool> stopping{false};
    std::atomic<bool> finish_requested{false};
};

PipelineExecutor::PipelineExecutor()
    : impl_(std::make_unique<Impl>()) {
}

PipelineExecutor::~PipelineExecutor() {
    impl_->stop();
}

void PipelineExecutor::configure(PipelineBuilderConfig config, RuntimeContext& runtime, SegmentRegistry& registry) {
    impl_->configure(std::move(config), runtime, registry);
}

void PipelineExecutor::set_stage_callback(PipelineStageRole role, StageCallback callback) {
    impl_->set_stage_callback(role, std::move(callback));
}

void PipelineExecutor::set_vad_stage(StageCallback callback) {
    impl_->set_vad_stage(std::move(callback));
}

void PipelineExecutor::set_feature_stage(StageCallback callback) {
    impl_->set_feature_stage(std::move(callback));
}

void PipelineExecutor::set_asr_stage(StageCallback callback) {
    impl_->set_asr_stage(std::move(callback));
}

void PipelineExecutor::set_event_stage(StageCallback callback) {
    impl_->set_event_stage(std::move(callback));
}

void PipelineExecutor::set_completion_callback(CompletionCallback callback) {
    impl_->set_completion_callback(std::move(callback));
}

void PipelineExecutor::start() {
    impl_->start();
}

void PipelineExecutor::push(PipelineToken token) {
    impl_->push(std::move(token));
}

void PipelineExecutor::finish() {
    impl_->finish();
}

void PipelineExecutor::stop() {
    impl_->stop();
}

void PipelineExecutor::wait() {
    impl_->wait();
}

bool PipelineExecutor::is_running() const {
    return impl_->is_running();
}

auto PipelineExecutor::pending_tokens() const -> std::size_t {
    return impl_->pending_tokens();
}

}
