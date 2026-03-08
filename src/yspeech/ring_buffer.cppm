module;

#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

export module yspeech.ring_buffer;

import std;

namespace yspeech {

export template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity) 
        : buffer_(capacity), capacity_(capacity) {}
    
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) noexcept = delete;
    RingBuffer& operator=(RingBuffer&&) noexcept = delete;
    
    bool push(const T& item) {
        std::unique_lock lock(mutex_);
        if (full()) {
            return false;
        }
        buffer_[write_idx_] = item;
        write_idx_ = (write_idx_ + 1) % capacity_;
        ++size_;
        cv_.notify_one();
        return true;
    }
    
    bool push(T&& item) {
        std::unique_lock lock(mutex_);
        if (full()) {
            return false;
        }
        buffer_[write_idx_] = std::move(item);
        write_idx_ = (write_idx_ + 1) % capacity_;
        ++size_;
        cv_.notify_one();
        return true;
    }
    
    bool pop(T& item) {
        std::unique_lock lock(mutex_);
        if (empty()) {
            return false;
        }
        item = std::move(buffer_[read_idx_]);
        read_idx_ = (read_idx_ + 1) % capacity_;
        --size_;
        return true;
    }
    
    bool pop_wait(T& item, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        std::unique_lock lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this]() { return !empty() || stopped_; })) {
            return false;
        }
        if (stopped_ || empty()) {
            return false;
        }
        item = std::move(buffer_[read_idx_]);
        read_idx_ = (read_idx_ + 1) % capacity_;
        --size_;
        return true;
    }
    
    void stop() {
        std::unique_lock lock(mutex_);
        stopped_ = true;
        cv_.notify_all();
    }
    
    void reset() {
        std::unique_lock lock(mutex_);
        read_idx_ = 0;
        write_idx_ = 0;
        size_ = 0;
        stopped_ = false;
    }
    
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == capacity_; }
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

private:
    std::vector<T> buffer_;
    size_t capacity_;
    size_t read_idx_ = 0;
    size_t write_idx_ = 0;
    size_t size_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
};

}
