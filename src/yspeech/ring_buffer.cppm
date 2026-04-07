module;

export module yspeech.ring_buffer;

import std;

namespace yspeech {

export template <typename T> class RingBuffer {
public:
  explicit RingBuffer(std::size_t capacity)
      : buffer_(capacity), capacity_(capacity) {}

  RingBuffer(const RingBuffer &) = delete;
  RingBuffer &operator=(const RingBuffer &) = delete;
  RingBuffer(RingBuffer &&) noexcept = delete;
  RingBuffer &operator=(RingBuffer &&) noexcept = delete;

  bool push(const T &item) {
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

  bool push(T &&item) {
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

  std::size_t push_batch(const T *items, std::size_t count) {
    std::unique_lock lock(mutex_);
    std::size_t available = capacity_ - size_;
    std::size_t to_write = std::min(count, available);

    for (std::size_t i = 0; i < to_write; ++i) {
      buffer_[write_idx_] = items[i];
      write_idx_ = (write_idx_ + 1) % capacity_;
      ++size_;
    }

    if (to_write > 0) {
      cv_.notify_one();
    }

    return to_write;
  }

  std::size_t push_batch(std::span<const T> items) {
    return push_batch(items.data(), items.size());
  }

  bool pop(T &item) {
    std::unique_lock lock(mutex_);
    if (empty()) {
      return false;
    }
    item = std::move(buffer_[read_idx_]);
    read_idx_ = (read_idx_ + 1) % capacity_;
    --size_;
    return true;
  }

  bool pop_wait(T &item, std::chrono::milliseconds timeout =
                             std::chrono::milliseconds(1000)) {
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, timeout,
                      [this]() { return !empty() || stopped_; })) {
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

  std::size_t pop_batch(T *items, std::size_t count) {
    std::unique_lock lock(mutex_);
    std::size_t to_read = std::min(count, size_);

    for (std::size_t i = 0; i < to_read; ++i) {
      items[i] = std::move(buffer_[read_idx_]);
      read_idx_ = (read_idx_ + 1) % capacity_;
      --size_;
    }

    return to_read;
  }

  std::size_t pop_batch(std::span<T> items) {
    return pop_batch(items.data(), items.size());
  }

  std::size_t pop_batch_wait(
      T *items, std::size_t count,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, timeout,
                      [this]() { return !empty() || stopped_; })) {
      return 0;
    }
    if (stopped_ || empty()) {
      return 0;
    }
    std::size_t to_read = std::min(count, size_);

    for (std::size_t i = 0; i < to_read; ++i) {
      items[i] = std::move(buffer_[read_idx_]);
      read_idx_ = (read_idx_ + 1) % capacity_;
      --size_;
    }

    return to_read;
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
  std::size_t size() const { return size_; }
  std::size_t capacity() const { return capacity_; }
  std::size_t available() const { return capacity_ - size_; }

private:
  std::vector<T> buffer_;
  std::size_t capacity_;
  std::size_t read_idx_ = 0;
  std::size_t write_idx_ = 0;
  std::size_t size_ = 0;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool stopped_ = false;
};

} // namespace yspeech
