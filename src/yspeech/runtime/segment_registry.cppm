module;

export module yspeech.runtime.segment_registry;

import std;
import yspeech.runtime.token;
import yspeech.runtime.segment_state;

namespace yspeech {

export class SegmentRegistry {
public:
    auto create_segment(std::string stream_id, std::int64_t start_ms) -> std::shared_ptr<SegmentState> {
        auto segment = std::make_shared<SegmentState>();
        {
            std::lock_guard lock(mutex_);
            segment->segment_id = next_segment_id_++;
            segment->stream_id = std::move(stream_id);
            segment->start_ms = start_ms;
            segment->end_ms = start_ms;
            segments_.emplace(segment->segment_id, segment);
        }
        return segment;
    }

    auto ensure_segment(SegmentId segment_id, std::string stream_id, std::int64_t start_ms)
        -> std::shared_ptr<SegmentState> {
        std::lock_guard lock(mutex_);
        if (auto it = segments_.find(segment_id); it != segments_.end()) {
            return it->second;
        }

        auto segment = std::make_shared<SegmentState>();
        segment->segment_id = segment_id;
        segment->stream_id = std::move(stream_id);
        segment->start_ms = start_ms;
        segment->end_ms = start_ms;
        next_segment_id_ = std::max(next_segment_id_, segment_id + 1);
        segments_.emplace(segment_id, segment);
        return segment;
    }

    auto get(SegmentId segment_id) const -> std::shared_ptr<SegmentState> {
        std::lock_guard lock(mutex_);
        if (auto it = segments_.find(segment_id); it != segments_.end()) {
            return it->second;
        }
        return {};
    }

    auto close_segment(SegmentId segment_id, std::int64_t end_ms) -> bool {
        auto segment = get(segment_id);
        if (!segment) {
            return false;
        }

        std::lock_guard lock(segment->mutex);
        segment->end_ms = end_ms;
        segment->lifecycle = SegmentLifecycle::Closed;
        return true;
    }

    void erase_closed() {
        std::lock_guard lock(mutex_);
        std::erase_if(segments_, [](const auto& entry) {
            const auto& segment = entry.second;
            if (!segment) {
                return true;
            }
            std::lock_guard segment_lock(segment->mutex);
            if (segment->lifecycle == SegmentLifecycle::Closed && segment->final_emitted) {
                segment->audio_accumulated.clear();
                segment->audio_accumulated.shrink_to_fit();
                segment->features_accumulated.clear();
                segment->features_accumulated.shrink_to_fit();
                segment->last_partial.reset();
                segment->final_result.reset();
            }
            return segment->lifecycle == SegmentLifecycle::Closed && segment->final_emitted;
        });
    }

    auto active_segments() const -> std::vector<SegmentId> {
        std::vector<SegmentId> ids;
        std::lock_guard lock(mutex_);
        ids.reserve(segments_.size());
        for (const auto& [segment_id, segment] : segments_) {
            if (!segment) {
                continue;
            }
            std::lock_guard segment_lock(segment->mutex);
            if (segment->lifecycle != SegmentLifecycle::Closed) {
                ids.push_back(segment_id);
            }
        }
        return ids;
    }

    auto snapshot_ordered() const -> std::vector<std::shared_ptr<SegmentState>> {
        std::vector<std::shared_ptr<SegmentState>> items;
        {
            std::lock_guard lock(mutex_);
            items.reserve(segments_.size());
            for (const auto& [segment_id, segment] : segments_) {
                (void)segment_id;
                if (segment) {
                    items.push_back(segment);
                }
            }
        }
        std::sort(items.begin(), items.end(), [](const auto& lhs, const auto& rhs) {
            if (!lhs || !rhs) {
                return static_cast<bool>(lhs);
            }
            if (lhs->start_ms != rhs->start_ms) {
                return lhs->start_ms < rhs->start_ms;
            }
            return lhs->segment_id < rhs->segment_id;
        });
        return items;
    }

    void clear() {
        std::lock_guard lock(mutex_);
        segments_.clear();
        next_segment_id_ = 1;
    }

    auto size() const -> std::size_t {
        std::lock_guard lock(mutex_);
        return segments_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<SegmentId, std::shared_ptr<SegmentState>> segments_;
    SegmentId next_segment_id_ = 1;
};

}
