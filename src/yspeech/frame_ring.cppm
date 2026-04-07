export module yspeech.frame_ring;

import std;
import yspeech.types;

namespace yspeech {

export enum class FrameReadStatus {
    Ok,
    Empty,
    Overrun,
    Eof
};

export struct FrameReadResult {
    FrameReadStatus status = FrameReadStatus::Empty;
    AudioFramePtr frame;
    std::uint64_t requested_seq = 0;
    std::uint64_t oldest_available_seq = 0;
    std::uint64_t next_write_seq = 0;
};

export struct FrameRingStats {
    std::uint64_t next_write_seq = 0;
    std::uint64_t oldest_seq = 0;
    std::size_t size = 0;
    std::size_t capacity = 0;
    std::uint64_t overwrite_count = 0;
};

export class FrameRing {
public:
    explicit FrameRing(std::size_t capacity)
        : slots_(std::max<std::size_t>(capacity, 1)),
          capacity_(std::max<std::size_t>(capacity, 1)) {
    }

    bool push(AudioFramePtr frame) {
        if (!frame) {
            return false;
        }

        std::unique_lock lock(mutex_);
        const std::uint64_t write_seq = next_write_seq_;
        if ((next_write_seq_ - oldest_seq_) >= static_cast<std::uint64_t>(capacity_)) {
            prune_to_locked(oldest_seq_ + 1);
            ++overwrite_count_;
        }
        auto& slot = slots_[static_cast<std::size_t>(write_seq % capacity_)];
        slot.seq = write_seq;
        slot.frame = std::move(frame);
        slot.occupied = true;

        ++next_write_seq_;
        return true;
    }

    FrameReadResult read(std::uint64_t seq) const {
        std::shared_lock lock(mutex_);

        FrameReadResult result;
        result.requested_seq = seq;
        result.oldest_available_seq = oldest_seq_locked();
        result.next_write_seq = next_write_seq_;

        if (seq < result.oldest_available_seq) {
            result.status = FrameReadStatus::Overrun;
            return result;
        }

        if (seq >= next_write_seq_) {
            result.status = FrameReadStatus::Empty;
            return result;
        }

        const auto& slot = slots_[static_cast<std::size_t>(seq % capacity_)];
        if (!slot.occupied || slot.seq != seq || !slot.frame) {
            result.status = FrameReadStatus::Overrun;
            return result;
        }

        result.status = FrameReadStatus::Ok;
        result.frame = slot.frame;
        if (result.frame->eos) {
            result.status = FrameReadStatus::Eof;
        }
        return result;
    }

    void clear() {
        std::unique_lock lock(mutex_);
        for (auto& slot : slots_) {
            slot.frame.reset();
            slot.seq = 0;
            slot.occupied = false;
        }
        oldest_seq_ = 0;
        next_write_seq_ = 0;
        overwrite_count_ = 0;
    }

    void prune_to(std::uint64_t seq_exclusive) {
        std::unique_lock lock(mutex_);
        prune_to_locked(seq_exclusive);
    }

    std::uint64_t oldest_seq() const {
        std::shared_lock lock(mutex_);
        return oldest_seq_locked();
    }

    std::uint64_t next_write_seq() const {
        std::shared_lock lock(mutex_);
        return next_write_seq_;
    }

    std::size_t size() const {
        std::shared_lock lock(mutex_);
        return static_cast<std::size_t>(next_write_seq_ - oldest_seq_);
    }

    std::size_t capacity() const {
        return capacity_;
    }

    FrameRingStats stats() const {
        std::shared_lock lock(mutex_);
        return FrameRingStats{
            .next_write_seq = next_write_seq_,
            .oldest_seq = oldest_seq_locked(),
            .size = static_cast<std::size_t>(next_write_seq_ - oldest_seq_),
            .capacity = capacity_,
            .overwrite_count = overwrite_count_
        };
    }

private:
    struct Slot {
        std::uint64_t seq = 0;
        AudioFramePtr frame;
        bool occupied = false;
    };

    std::uint64_t oldest_seq_locked() const {
        return oldest_seq_;
    }

    void prune_to_locked(std::uint64_t seq_exclusive) {
        const auto target = std::min(seq_exclusive, next_write_seq_);
        while (oldest_seq_ < target) {
            auto& slot = slots_[static_cast<std::size_t>(oldest_seq_ % capacity_)];
            if (slot.occupied && slot.seq == oldest_seq_) {
                slot.frame.reset();
                slot.occupied = false;
                slot.seq = 0;
            }
            ++oldest_seq_;
        }
    }

    mutable std::shared_mutex mutex_;
    std::vector<Slot> slots_;
    std::size_t capacity_ = 1;
    std::uint64_t oldest_seq_ = 0;
    std::uint64_t next_write_seq_ = 0;
    std::uint64_t overwrite_count_ = 0;
};

export class FrameReader {
public:
    FrameReader(std::shared_ptr<FrameRing> ring, std::string reader_id, std::uint64_t start_seq = 0)
        : ring_(std::move(ring)),
          reader_id_(std::move(reader_id)),
          next_seq_(start_seq) {
    }

    FrameReadResult next() {
        if (!ring_) {
            return {};
        }

        auto result = ring_->read(next_seq_);
        if (result.status == FrameReadStatus::Ok || result.status == FrameReadStatus::Eof) {
            ++next_seq_;
        }
        return result;
    }

    void seek_to_oldest() {
        if (!ring_) {
            next_seq_ = 0;
            return;
        }
        next_seq_ = ring_->oldest_seq();
    }

    void reset(std::uint64_t seq = 0) {
        next_seq_ = seq;
    }

    std::uint64_t next_seq() const {
        return next_seq_;
    }

    std::string_view id() const noexcept {
        return reader_id_;
    }

private:
    std::shared_ptr<FrameRing> ring_;
    std::string reader_id_;
    std::uint64_t next_seq_ = 0;
};

} // namespace yspeech
