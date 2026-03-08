export module yspeech.state;

import std;

namespace yspeech {

export struct State {
    std::atomic<bool> has_error{false};
    std::atomic<int> total_errors{0};
    std::atomic<int> recovered_errors{0};
    std::atomic<int> skipped_operators{0};
    
    void mark_error() {
        has_error.store(true, std::memory_order_relaxed);
        total_errors.fetch_add(1, std::memory_order_relaxed);
    }
    
    void mark_recovered() {
        recovered_errors.fetch_add(1, std::memory_order_relaxed);
    }
    
    void mark_skipped() {
        skipped_operators.fetch_add(1, std::memory_order_relaxed);
    }
    
    void reset() {
        has_error.store(false, std::memory_order_relaxed);
        total_errors.store(0, std::memory_order_relaxed);
        recovered_errors.store(0, std::memory_order_relaxed);
        skipped_operators.store(0, std::memory_order_relaxed);
    }
};

}
