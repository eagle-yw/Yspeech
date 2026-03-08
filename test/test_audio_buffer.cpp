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

TEST(AudioBufferTest, InitAudioBuffer) {
    yspeech::Context ctx;
    
    ctx.init_audio_buffer("audio", 2, 16000);
    
    EXPECT_EQ(ctx.audio_buffer_available("audio"), 0);
}

TEST(AudioBufferTest, WriteReadInterleaved) {
    yspeech::Context ctx;
    
    ctx.init_audio_buffer("audio", 2, 100);
    
    float interleaved[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    EXPECT_TRUE(ctx.audio_buffer_write_interleaved("audio", interleaved, 3, 16000));
    
    EXPECT_EQ(ctx.audio_buffer_available("audio"), 3);
    
    yspeech::AudioData audio;
    EXPECT_TRUE(ctx.audio_buffer_read("audio", audio, 2));
    
    EXPECT_EQ(audio.num_channels, 2);
    EXPECT_EQ(audio.num_samples(), 2);
    EXPECT_EQ(audio.channels[0].size(), 2);
    EXPECT_EQ(audio.channels[1].size(), 2);
    
    EXPECT_FLOAT_EQ(audio.channels[0][0], 1.0f);
    EXPECT_FLOAT_EQ(audio.channels[1][0], 2.0f);
    EXPECT_FLOAT_EQ(audio.channels[0][1], 3.0f);
    EXPECT_FLOAT_EQ(audio.channels[1][1], 4.0f);
}

TEST(AudioBufferTest, WriteReadPlanar) {
    yspeech::Context ctx;
    
    ctx.init_audio_buffer("audio", 2, 100);
    
    float ch0[] = {1.0f, 2.0f, 3.0f};
    float ch1[] = {4.0f, 5.0f, 6.0f};
    const float* channels[] = {ch0, ch1};
    
    EXPECT_TRUE(ctx.audio_buffer_write_planar("audio", channels, 3, 16000));
    
    EXPECT_EQ(ctx.audio_buffer_available("audio"), 3);
    
    yspeech::AudioData audio;
    EXPECT_TRUE(ctx.audio_buffer_read("audio", audio, 2));
    
    EXPECT_EQ(audio.num_channels, 2);
    EXPECT_EQ(audio.num_samples(), 2);
    
    EXPECT_FLOAT_EQ(audio.channels[0][0], 1.0f);
    EXPECT_FLOAT_EQ(audio.channels[0][1], 2.0f);
    EXPECT_FLOAT_EQ(audio.channels[1][0], 4.0f);
    EXPECT_FLOAT_EQ(audio.channels[1][1], 5.0f);
}

TEST(AudioBufferTest, DifferentChunkSizes) {
    yspeech::Context ctx;
    
    ctx.init_audio_buffer("audio", 1, 1000);
    
    float data[100];
    for (int i = 0; i < 100; ++i) {
        data[i] = static_cast<float>(i);
    }
    
    ctx.audio_buffer_write_interleaved("audio", data, 100, 16000);
    
    yspeech::AudioData audio1;
    EXPECT_TRUE(ctx.audio_buffer_read("audio", audio1, 30));
    EXPECT_EQ(audio1.num_samples(), 30);
    
    yspeech::AudioData audio2;
    EXPECT_TRUE(ctx.audio_buffer_read("audio", audio2, 50));
    EXPECT_EQ(audio2.num_samples(), 50);
    
    yspeech::AudioData audio3;
    EXPECT_TRUE(ctx.audio_buffer_read("audio", audio3, 20));
    EXPECT_EQ(audio3.num_samples(), 20);
    
    EXPECT_EQ(ctx.audio_buffer_available("audio"), 0);
}

TEST(AudioBufferTest, ThreadSafety) {
    yspeech::Context ctx;
    
    ctx.init_audio_buffer("audio", 2, 10000);
    
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    
    std::thread producer([&ctx, &produced]() {
        for (int i = 0; i < 100; ++i) {
            float data[20];
            for (int j = 0; j < 20; ++j) {
                data[j] = static_cast<float>(i * 20 + j);
            }
            if (ctx.audio_buffer_write_interleaved("audio", data, 10, 16000)) {
                produced += 10;
            }
        }
    });
    
    std::thread consumer([&ctx, &consumed]() {
        while (consumed < 1000) {
            yspeech::AudioData audio;
            if (ctx.audio_buffer_read("audio", audio, 10)) {
                consumed += 10;
            }
        }
    });
    
    producer.join();
    consumer.join();
}

TEST(AudioInputTest, InterleavedToPlanarConversion) {
    yspeech::Context ctx;
    
    ctx.init_audio_buffer("audio_planar", 2, 1000);
    
    int16_t pcm_data[] = {16384, -16384, 8192, -8192, 0, 0};
    std::vector<float> float_data;
    for (int i = 0; i < 6; ++i) {
        float_data.push_back(static_cast<float>(pcm_data[i]) / 32768.0f);
    }
    
    ctx.audio_buffer_write_interleaved("audio_planar", float_data.data(), 3, 16000);
    
    yspeech::AudioData audio;
    EXPECT_TRUE(ctx.audio_buffer_read("audio_planar", audio, 3));
    
    EXPECT_EQ(audio.num_channels, 2);
    EXPECT_EQ(audio.num_samples(), 3);
    
    EXPECT_NEAR(audio.channels[0][0], 0.5f, 0.001f);
    EXPECT_NEAR(audio.channels[1][0], -0.5f, 0.001f);
    EXPECT_NEAR(audio.channels[0][1], 0.25f, 0.001f);
    EXPECT_NEAR(audio.channels[1][1], -0.25f, 0.001f);
}
