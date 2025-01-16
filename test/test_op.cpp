#include "gtest/gtest.h"

import std;
import yspeech;

TEST(TestOperator, TestVad) {
    auto op = yspeech::OpVad();
    op.load("./temp/models/vad");
}