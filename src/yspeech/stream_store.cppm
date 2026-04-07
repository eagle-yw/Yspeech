export module yspeech.stream_store;

import std;
import yspeech.frame_ring;
import yspeech.types;

namespace yspeech {

export struct StreamRingConfig {
    std::size_t capacity_frames = 6000;
};

export struct StreamReaderInfo {
    std::string ring_key;
    std::string reader_key;
};

export class StreamStore {
public:
    void init_audio_ring(const std::string& key, std::size_t capacity_frames) {
        std::unique_lock lock(mutex_);
        auto stream = std::make_shared<StreamInternal>();
        stream->ring = std::make_shared<FrameRing>(capacity_frames);
        streams_[key] = std::move(stream);
    }

    bool has_ring(const std::string& key) const {
        std::shared_lock lock(mutex_);
        return streams_.contains(key);
    }

    bool push_frame(const std::string& ring_key, AudioFramePtr frame) {
        auto stream = get_stream(ring_key);
        if (!stream || !stream->ring) {
            return false;
        }
        const bool pushed = stream->ring->push(std::move(frame));
        if (pushed) {
            cleanup_consumed_locked(*stream);
        }
        return pushed;
    }

    void register_reader(const std::string& ring_key,
                         const std::string& reader_key,
                         std::uint64_t start_seq = 0) {
        auto stream = get_stream(ring_key);
        if (!stream || !stream->ring) {
            return;
        }

        std::unique_lock lock(mutex_);
        stream->readers[reader_key] = std::make_shared<FrameReader>(stream->ring, reader_key, start_seq);
        cleanup_consumed_locked(*stream);
    }

    FrameReadResult read_frame(const std::string& ring_key,
                               const std::string& reader_key) {
        auto reader = get_reader(ring_key, reader_key);
        if (!reader) {
            return {};
        }
        auto result = reader->next();
        if (result.status == FrameReadStatus::Ok || result.status == FrameReadStatus::Eof) {
            if (auto stream = get_stream(ring_key)) {
                std::unique_lock lock(mutex_);
                cleanup_consumed_locked(*stream);
            }
        }
        return result;
    }

    void seek_reader_to_oldest(const std::string& ring_key,
                               const std::string& reader_key) {
        auto reader = get_reader(ring_key, reader_key);
        if (reader) {
            reader->seek_to_oldest();
            if (auto stream = get_stream(ring_key)) {
                std::unique_lock lock(mutex_);
                cleanup_consumed_locked(*stream);
            }
        }
    }

    void reset_reader(const std::string& ring_key,
                      const std::string& reader_key,
                      std::uint64_t seq = 0) {
        auto reader = get_reader(ring_key, reader_key);
        if (reader) {
            reader->reset(seq);
            if (auto stream = get_stream(ring_key)) {
                std::unique_lock lock(mutex_);
                cleanup_consumed_locked(*stream);
            }
        }
    }

    void unregister_reader(const std::string& ring_key, const std::string& reader_key) {
        auto stream = get_stream(ring_key);
        if (!stream) {
            return;
        }

        std::unique_lock lock(mutex_);
        stream->readers.erase(reader_key);
        cleanup_consumed_locked(*stream);
    }

    void cleanup_consumed(const std::string& ring_key) {
        auto stream = get_stream(ring_key);
        if (!stream) {
            return;
        }

        std::unique_lock lock(mutex_);
        cleanup_consumed_locked(*stream);
    }

    FrameRingStats ring_stats(const std::string& ring_key) const {
        auto ring = get_ring(ring_key);
        return ring ? ring->stats() : FrameRingStats{};
    }

    bool has_unread(const std::string& ring_key, const std::string& reader_key) {
        auto ring = get_ring(ring_key);
        auto reader = get_reader(ring_key, reader_key);
        if (!ring || !reader) {
            return false;
        }
        return reader->next_seq() < ring->next_write_seq();
    }

private:
    struct StreamInternal {
        std::shared_ptr<FrameRing> ring;
        std::unordered_map<std::string, std::shared_ptr<FrameReader>> readers;
    };

    std::shared_ptr<StreamInternal> get_stream(const std::string& ring_key) const {
        std::shared_lock lock(mutex_);
        auto it = streams_.find(ring_key);
        if (it == streams_.end()) {
            return nullptr;
        }
        return it->second;
    }

    std::shared_ptr<FrameRing> get_ring(const std::string& ring_key) const {
        auto stream = get_stream(ring_key);
        return stream ? stream->ring : nullptr;
    }

    std::shared_ptr<FrameReader> get_reader(const std::string& ring_key,
                                            const std::string& reader_key) {
        auto stream = get_stream(ring_key);
        if (!stream || !stream->ring) {
            return nullptr;
        }

        {
            std::shared_lock lock(mutex_);
            auto it = stream->readers.find(reader_key);
            if (it != stream->readers.end()) {
                return it->second;
            }
        }

        std::unique_lock lock(mutex_);
        auto& reader = stream->readers[reader_key];
        if (!reader) {
            reader = std::make_shared<FrameReader>(stream->ring, reader_key, stream->ring->oldest_seq());
        }
        return reader;
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<StreamInternal>> streams_;

    void cleanup_consumed_locked(StreamInternal& stream) {
        if (!stream.ring || stream.readers.empty()) {
            return;
        }

        std::uint64_t min_next_seq = stream.ring->next_write_seq();
        for (const auto& [_, reader] : stream.readers) {
            if (!reader) {
                continue;
            }
            min_next_seq = std::min(min_next_seq, reader->next_seq());
        }
        stream.ring->prune_to(min_next_seq);
    }
};

} // namespace yspeech
