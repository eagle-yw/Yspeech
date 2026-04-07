#include "gtest/gtest.h"

import yspeech.audio.file;
import yspeech.audio;
import yspeech;
import std;

TEST(TestAudio, TestAudioFile) {
    auto s = yspeech::AudioFileStream("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav");
    auto bytes = yspeech::Bytes();
    bytes.resize(1024);
    auto stream = yspeech::AudioStreamIface(std::move(s));
    auto size = yspeech::Size(0);
    auto total = yspeech::Size(0);
    do{
        size = stream.read(bytes.data(), bytes.size());
        total += size;
    }while(size > 0);
    EXPECT_GT(total, 0);
}
