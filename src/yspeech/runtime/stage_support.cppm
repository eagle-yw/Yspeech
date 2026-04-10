export module yspeech.runtime.stage_support;

import std;
import yspeech.types;

namespace yspeech {

namespace detail {

template <typename Core>
concept StatsBindableCore = requires(Core& core, ProcessingStats* stats) {
    core.bind_stats(stats);
};

} // namespace detail

export class StageStatsBinding {
public:
    void bind(ProcessingStats* stats) {
        stats_ = stats;
    }

    auto current() const -> ProcessingStats* {
        return stats_;
    }

    template <typename Core>
    void bind_core(Core& core) const {
        if constexpr (detail::StatsBindableCore<Core>) {
            core.bind_stats(stats_);
        }
    }

    template <typename CorePtr>
    void bind_core_ptr(const CorePtr& core) const {
        if (core) {
            bind_core(*core);
        }
    }

private:
    ProcessingStats* stats_ = nullptr;
};

export template <typename CoreIface>
struct LazyCoreHolder {
    std::unique_ptr<CoreIface> core;
    bool initialized = false;

    template <typename CreateFn, typename InitFn>
    auto ensure(CreateFn&& create_fn, InitFn&& init_fn) -> CoreIface* {
        if (!initialized) {
            core = std::invoke(std::forward<CreateFn>(create_fn));
            if (core) {
                std::invoke(std::forward<InitFn>(init_fn), *core);
                initialized = true;
            }
        }
        return core.get();
    }

    void reset() {
        initialized = false;
        core.reset();
    }
};

export template <typename CoreIface>
struct MutexCoreHolder {
    std::mutex mutex;
    std::unique_ptr<CoreIface> core;
};

export template <typename CoreIface>
inline void deinit_core(std::unique_ptr<CoreIface>& core) {
    if (core) {
        core->deinit();
        core.reset();
    }
}

export template <typename HolderRange>
inline void deinit_holder_cores(HolderRange& holders) {
    for (auto& holder : holders) {
        if (!holder) {
            continue;
        }
        deinit_core(holder->core);
    }
}

}
