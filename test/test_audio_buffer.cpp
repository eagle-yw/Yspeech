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
