#include "gtest/gtest.h"
#include <thread>
#include <cmath>
#include <nlohmann/json.hpp>
import yspeech;

TEST(AudioBufferTest, RingBufferBasic) {
    yspeech::RingBuffer<int> buffer(10);
    
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.capacity(), 10);
    
    EXPECT_TRUE(buffer.push(1));
    EXPECT_TRUE(buffer.push(2));
    EXPECT_TRUE(buffer.push(3));
    
    EXPECT_EQ(buffer.size(), 3);
    
    int value;
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 3);
    
    EXPECT_TRUE(buffer.empty());
}

TEST(AudioBufferTest, RingBufferFull) {
    yspeech::RingBuffer<int> buffer(3);
    
    EXPECT_TRUE(buffer.push(1));
    EXPECT_TRUE(buffer.push(2));
    EXPECT_TRUE(buffer.push(3));
    EXPECT_TRUE(buffer.full());
    EXPECT_FALSE(buffer.push(4));
}

TEST(AudioBufferTest, RingBufferWrapAround) {
    yspeech::RingBuffer<int> buffer(3);
    
    EXPECT_TRUE(buffer.push(1));
    EXPECT_TRUE(buffer.push(2));
    EXPECT_TRUE(buffer.push(3));
    
    int value;
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 2);
    
    EXPECT_TRUE(buffer.push(4));
    EXPECT_TRUE(buffer.push(5));
    
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 3);
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 4);
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 5);
}

namespace {

yspeech::AudioFramePtr make_frame(const std::vector<float>& samples,
                                  std::uint64_t seq,
                                  bool eos = false,
                                  int channels = 1,
                                  std::int64_t pts_ms = 0) {
    static yspeech::AudioFramePool pool;
    auto frame = pool.acquire(samples.size());
    frame->stream_id = "test";
    frame->seq = seq;
    frame->sample_rate = 16000;
    frame->channels = channels;
    frame->pts_ms = pts_ms;
    frame->dur_ms = 10;
    frame->eos = eos;
    frame->samples = samples;
    return frame;
}

}

TEST(AudioBufferTest, InitAudioStreamRing) {
    yspeech::StreamStore store;
    store.init_audio_ring("audio", 16);
    EXPECT_TRUE(store.has_ring("audio"));
    EXPECT_FALSE(store.has_unread("audio", "reader"));
}

TEST(AudioBufferTest, PushReadFrames) {
    yspeech::StreamStore store;
    store.init_audio_ring("audio", 16);

    EXPECT_TRUE(store.push_frame("audio", make_frame({1.0f, 2.0f, 3.0f}, 0)));
    EXPECT_TRUE(store.push_frame("audio", make_frame({4.0f, 5.0f, 6.0f}, 1, true)));

    auto first = store.read_frame("audio", "reader");
    ASSERT_EQ(first.status, yspeech::FrameReadStatus::Ok);
    ASSERT_TRUE(first.frame);
    EXPECT_EQ(first.frame->samples.size(), 3);
    EXPECT_FLOAT_EQ(first.frame->samples[0], 1.0f);

    auto second = store.read_frame("audio", "reader");
    ASSERT_TRUE(second.status == yspeech::FrameReadStatus::Ok || second.status == yspeech::FrameReadStatus::Eof);
    ASSERT_TRUE(second.frame);
    EXPECT_TRUE(second.frame->eos);
}

TEST(AudioBufferTest, ReaderProgressByChunk) {
    yspeech::StreamStore store;
    store.init_audio_ring("audio", 32);

    for (std::uint64_t i = 0; i < 5; ++i) {
        EXPECT_TRUE(store.push_frame("audio", make_frame({static_cast<float>(i)}, i, i == 4)));
    }

    int consumed = 0;
    while (store.has_unread("audio", "reader")) {
        auto result = store.read_frame("audio", "reader");
        if (!result.frame) {
            continue;
        }
        consumed++;
    }

    EXPECT_EQ(consumed, 5);
    EXPECT_FALSE(store.has_unread("audio", "reader"));
}

TEST(AudioBufferTest, ThreadSafety) {
    yspeech::StreamStore store;
    store.init_audio_ring("audio", 2048);

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::thread producer([&]() {
        for (std::uint64_t i = 0; i < 100; ++i) {
            if (store.push_frame("audio", make_frame({static_cast<float>(i)}, i, i == 99))) {
                produced++;
            }
        }
    });

    std::thread consumer([&]() {
        while (consumed < 100) {
            auto result = store.read_frame("audio", "reader");
            if (result.frame) {
                consumed++;
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(produced.load(), 100);
    EXPECT_EQ(consumed.load(), 100);
}

TEST(AudioInputTest, InterleavedFrameLayout) {
    yspeech::StreamStore store;
    store.init_audio_ring("audio", 4);

    std::vector<float> interleaved = {0.5f, -0.5f, 0.25f, -0.25f, 0.0f, 0.0f};
    EXPECT_TRUE(store.push_frame("audio", make_frame(interleaved, 0, true, 2)));

    auto result = store.read_frame("audio", "reader");
    ASSERT_TRUE(result.frame);
    EXPECT_EQ(result.frame->channels, 2);
    EXPECT_EQ(result.frame->samples_per_channel(), 3);
    EXPECT_NEAR(result.frame->samples[0], 0.5f, 0.001f);
    EXPECT_NEAR(result.frame->samples[1], -0.5f, 0.001f);
    EXPECT_NEAR(result.frame->samples[2], 0.25f, 0.001f);
    EXPECT_NEAR(result.frame->samples[3], -0.25f, 0.001f);
}
