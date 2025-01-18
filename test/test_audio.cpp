#include "gtest/gtest.h"

import std;
import yspeech;

TEST(TestAudio, TestAudioFile) {
    auto s = yspeech::AudioFileStream("./temp/audio/test_wavs/0.wav");
    auto bytes = yspeech::Bytes();
    bytes.resize(1024);
    auto stream = yspeech::AudioStreamIface(std::move(s));
    auto size = yspeech::Size(0);
    do{
        size = stream.read(bytes.data(), bytes.size());
    }while(size > 0);
}