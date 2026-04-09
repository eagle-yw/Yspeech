module;

#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/data_pipeline.hpp>

export module yspeech.frame_source;

import std;
import yspeech.audio;
import yspeech.audio.file;
import yspeech.types;

namespace yspeech {

namespace detail {

inline int to_int_sample_rate(SampleRate rate) {
    return static_cast<int>(rate);
}

inline float pcm16_to_float(std::int16_t sample) {
    return static_cast<float>(sample) / 32768.0f;
}

}

export class IFrameSource {
public:
    virtual ~IFrameSource() = default;

    virtual bool next(AudioFramePtr& frame) = 0;
    virtual void stop() {}
    virtual std::string_view kind() const noexcept = 0;
};

export class MicSource : public IFrameSource {
public:
    explicit MicSource(std::string stream_id = "mic")
        : stream_id_(std::move(stream_id)) {
    }

    bool next(AudioFramePtr& frame) override {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this]() {
            return stopped_ || !queue_.empty();
        });

        if (queue_.empty()) {
            frame.reset();
            return false;
        }

        frame = std::move(queue_.front());
        queue_.pop();
        return static_cast<bool>(frame);
    }

    void push_frame(AudioFramePtr frame) {
        if (!frame) {
            return;
        }

        {
            std::lock_guard lock(mutex_);
            queue_.push(std::move(frame));
        }
        cv_.notify_one();
    }

    AudioFramePtr make_frame(std::vector<float> samples,
                             std::int64_t pts_ms,
                             std::int64_t dur_ms = 10,
                             bool eos = false,
                             bool gap = false,
                             int sample_rate = 16000,
                             int channels = 1) {
        auto frame = frame_pool_.acquire(samples.size());
        frame->stream_id = stream_id_;
        frame->seq = next_seq_++;
        frame->sample_rate = sample_rate;
        frame->channels = channels;
        frame->pts_ms = pts_ms;
        frame->dur_ms = dur_ms;
        frame->eos = eos;
        frame->gap = gap;
        frame->samples = std::move(samples);
        return frame;
    }

    void close(bool emit_eos = false, std::int64_t pts_ms = 0, int sample_rate = 16000, int channels = 1) {
        if (emit_eos) {
            push_frame(make_frame({}, pts_ms, 0, true, false, sample_rate, channels));
        }
        stop();
    }

    void stop() override {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    std::string_view kind() const noexcept override {
        return "microphone";
    }

private:
    std::string stream_id_;
    std::uint64_t next_seq_ = 0;
    AudioFramePool frame_pool_;
    std::queue<AudioFramePtr> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
};

export class StreamSource : public MicSource {
public:
    explicit StreamSource(std::string stream_id = "stream")
        : MicSource(std::move(stream_id)) {
    }

    std::string_view kind() const noexcept override {
        return "stream";
    }
};

export class FileSource : public IFrameSource {
public:
    explicit FileSource(const std::string& path,
                        std::string stream_id = "file",
                        double playback_rate = 1.0)
        : stream_(path),
          stream_id_(std::move(stream_id)),
          playback_rate_(playback_rate) {
        sample_rate_ = detail::to_int_sample_rate(stream_.sampleRate());
        channels_ = std::max(stream_.micNum(), 1);
        samples_per_frame_per_channel_ = static_cast<std::size_t>(sample_rate_) / 100;
        samples_per_frame_ = samples_per_frame_per_channel_ * static_cast<std::size_t>(channels_);
        bytes_per_frame_ = samples_per_frame_ * sizeof(std::int16_t);
        frame_bytes_.resize(bytes_per_frame_);
    }

    bool next(AudioFramePtr& frame) override {
        if (stopped_ || finished_) {
            frame.reset();
            return false;
        }

        pace_if_needed();

        auto bytes_read = stream_.read(frame_bytes_.data(), frame_bytes_.size());
        if (bytes_read == 0) {
            finished_ = true;
            frame.reset();
            return false;
        }

        auto audio_frame = frame_pool_.acquire(samples_per_frame_);
        audio_frame->samples.resize(samples_per_frame_, 0.0f);
        auto sample_count = bytes_read / sizeof(std::int16_t);
        auto* pcm = reinterpret_cast<const std::int16_t*>(frame_bytes_.data());
        for (std::size_t i = 0; i < sample_count; ++i) {
            audio_frame->samples[i] = detail::pcm16_to_float(pcm[i]);
        }

        audio_frame->stream_id = stream_id_;
        audio_frame->seq = next_seq_++;
        audio_frame->sample_rate = sample_rate_;
        audio_frame->channels = channels_;
        audio_frame->pts_ms = next_pts_ms_;
        audio_frame->dur_ms = 10;
        audio_frame->eos = bytes_read < bytes_per_frame_;
        audio_frame->gap = false;

        next_pts_ms_ += audio_frame->dur_ms;
        if (audio_frame->eos) {
            finished_ = true;
        }

        frame = std::move(audio_frame);
        return true;
    }

    void stop() override {
        stopped_ = true;
    }

    std::string_view kind() const noexcept override {
        return "file";
    }

    void set_playback_rate(double playback_rate) {
        playback_rate_ = playback_rate;
    }
private:
    void pace_if_needed() {
        if (playback_rate_ <= 0.0) {
            return;
        }

        using namespace std::chrono;

        auto now = steady_clock::now();
        if (!playback_started_) {
            playback_started_ = true;
            playback_start_time_ = now;
            return;
        }

        const auto frame_interval = duration<double, std::milli>(10.0 / playback_rate_);
        auto expected_time = playback_start_time_ + duration_cast<steady_clock::duration>(
            frame_interval * static_cast<double>(next_seq_)
        );
        if (expected_time > now) {
            std::this_thread::sleep_until(expected_time);
        }
    }

    AudioFileStream stream_;
    std::string stream_id_;
    AudioFramePool frame_pool_;
    int sample_rate_ = 16000;
    int channels_ = 1;
    std::uint64_t next_seq_ = 0;
    std::int64_t next_pts_ms_ = 0;
    std::size_t samples_per_frame_per_channel_ = 160;
    std::size_t samples_per_frame_ = 160;
    std::size_t bytes_per_frame_ = 320;
    Bytes frame_bytes_;
    double playback_rate_ = 1.0;
    bool playback_started_ = false;
    std::chrono::steady_clock::time_point playback_start_time_{};
    bool stopped_ = false;
    bool finished_ = false;
};

export class AudioFramePipelineSource : public IFrameSource {
public:
    explicit AudioFramePipelineSource(std::shared_ptr<IFrameSource> upstream, std::size_t num_lines = 1)
        : upstream_(std::move(upstream)), num_lines_(std::max<std::size_t>(1, num_lines)) {
    }

    ~AudioFramePipelineSource() override {
        stop();
    }

    bool next(AudioFramePtr& frame) override {
        start_once();

        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this]() {
            return stopped_ || closed_ || !queue_.empty();
        });

        if (queue_.empty()) {
            frame.reset();
            return false;
        }

        frame = std::move(queue_.front());
        queue_.pop();
        return static_cast<bool>(frame);
    }

    void stop() override {
        bool expected = false;
        if (!stopped_.compare_exchange_strong(expected, true)) {
            return;
        }

        if (upstream_) {
            upstream_->stop();
        }

        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();

        if (worker_.joinable()) {
            worker_.join();
        }
    }

    std::string_view kind() const noexcept override {
        return "taskflow_pipeline";
    }

private:
    void start_once() {
        std::call_once(start_flag_, [this]() {
            worker_ = std::thread([this]() {
                tf::Taskflow taskflow("audio_frame_source");
                tf::Executor executor;

                tf::DataPipeline pipeline(num_lines_,
                    tf::make_data_pipe<void, AudioFramePtr>(tf::PipeType::SERIAL,
                        [this](tf::Pipeflow& pf) -> AudioFramePtr {
                            if (stopped_ || !upstream_) {
                                pf.stop();
                                return {};
                            }

                            AudioFramePtr frame;
                            if (!upstream_->next(frame) || !frame) {
                                pf.stop();
                                return {};
                            }

                            return frame;
                        }),
                    tf::make_data_pipe<AudioFramePtr, void>(tf::PipeType::SERIAL,
                        [this](AudioFramePtr& frame) {
                            if (!frame) {
                                return;
                            }

                            {
                                std::lock_guard lock(mutex_);
                                queue_.push(frame);
                            }
                            cv_.notify_one();
                        })
                );

                taskflow.composed_of(pipeline).name("audio_frame_pipeline_source");
                executor.run(taskflow).wait();

                {
                    std::lock_guard lock(mutex_);
                    closed_ = true;
                }
                cv_.notify_all();
            });
        });
    }

    std::shared_ptr<IFrameSource> upstream_;
    std::size_t num_lines_ = 1;
    std::once_flag start_flag_;
    std::thread worker_;
    std::queue<AudioFramePtr> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stopped_{false};
    bool closed_ = false;
};

}
