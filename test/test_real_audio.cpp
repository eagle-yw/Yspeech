#include "gtest/gtest.h"
#include <filesystem>
#include <chrono>

import yspeech.audio.file;
import yspeech.audio;
import yspeech.context;
import yspeech;
import yspeech.types;
import std;

namespace fs = std::filesystem;

class TestRealAudio : public ::testing::Test {
protected:
    static constexpr const char* AUDIO_DIR = "./data/Audio-Turing-Test-Audios/extracted_audio/";
    
    std::vector<fs::path> get_audio_files(const std::string& prefix) {
        std::vector<fs::path> files;
        if (!fs::exists(AUDIO_DIR)) {
            return files;
        }
        for (const auto& entry : fs::directory_iterator(AUDIO_DIR)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wav") {
                std::string filename = entry.path().filename().string();
                if (filename.find(prefix) == 0) {
                    files.push_back(entry.path());
                }
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }
    
    std::vector<fs::path> get_all_audio_files() {
        std::vector<fs::path> files;
        if (!fs::exists(AUDIO_DIR)) {
            return files;
        }
        for (const auto& entry : fs::directory_iterator(AUDIO_DIR)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wav") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }
};

TEST_F(TestRealAudio, BasicFileLoading) {
    auto ai_files = get_audio_files("ai_");
    auto human_files = get_audio_files("human_");
    
    ASSERT_FALSE(ai_files.empty()) << "No AI audio files found in " << AUDIO_DIR;
    ASSERT_FALSE(human_files.empty()) << "No human audio files found in " << AUDIO_DIR;
    
    auto ai_stream = yspeech::AudioFileStream(ai_files[0].string());
    EXPECT_GT(ai_stream.micNum(), 0);
    EXPECT_NE(ai_stream.sampleRate(), yspeech::SampleRate::SR_8000);
    
    auto human_stream = yspeech::AudioFileStream(human_files[0].string());
    EXPECT_GT(human_stream.micNum(), 0);
    EXPECT_NE(human_stream.sampleRate(), yspeech::SampleRate::SR_8000);
    
    yspeech::Bytes buffer;
    buffer.resize(4096);
    
    auto ai_audio_stream = yspeech::AudioStreamIface(std::move(ai_stream));
    auto ai_size = ai_audio_stream.read(buffer.data(), buffer.size());
    EXPECT_GT(ai_size, 0);
    
    auto human_audio_stream = yspeech::AudioStreamIface(std::move(human_stream));
    auto human_size = human_audio_stream.read(buffer.data(), buffer.size());
    EXPECT_GT(human_size, 0);
}

TEST_F(TestRealAudio, BatchFileLoading) {
    auto all_files = get_all_audio_files();
    
    if (all_files.empty()) {
        GTEST_SKIP() << "Audio directory not found: " << AUDIO_DIR;
    }
    
    EXPECT_EQ(all_files.size(), 104) << "Expected 104 audio files (35 AI + 69 human)";
    
    size_t success_count = 0;
    size_t ai_count = 0;
    size_t human_count = 0;
    
    for (const auto& path : all_files) {
        std::string filename = path.filename().string();
        try {
            auto stream = yspeech::AudioFileStream(path.string());
            EXPECT_GT(stream.micNum(), 0) << filename << ": Invalid channel count";
            EXPECT_NE(stream.sampleRate(), yspeech::SampleRate::SR_8000) 
                << filename << ": Invalid sample rate";
            
            yspeech::Bytes buffer;
            buffer.resize(8192);
            auto audio_stream = yspeech::AudioStreamIface(std::move(stream));
            auto size = audio_stream.read(buffer.data(), buffer.size());
            EXPECT_GT(size, 0) << filename << ": Could not read audio data";
            
            success_count++;
            if (filename.find("ai_") == 0) ai_count++;
            else if (filename.find("human_") == 0) human_count++;
        } catch (const std::exception& e) {
            ADD_FAILURE() << "Failed to load " << filename << ": " << e.what();
        }
    }
    
    EXPECT_EQ(success_count, all_files.size());
    EXPECT_EQ(ai_count, 35);
    EXPECT_EQ(human_count, 69);
}

TEST_F(TestRealAudio, FileProperties) {
    auto all_files = get_all_audio_files();
    
    if (all_files.empty()) {
        GTEST_SKIP() << "Audio directory not found: " << AUDIO_DIR;
    }
    
    std::map<yspeech::SampleRate, size_t> sample_rate_dist;
    std::map<int, size_t> channel_dist;
    std::map<std::string, size_t> prefix_dist;
    
    for (const auto& path : all_files) {
        std::string filename = path.filename().string();
        try {
            auto stream = yspeech::AudioFileStream(path.string());
            sample_rate_dist[stream.sampleRate()]++;
            channel_dist[stream.micNum()]++;
            
            if (filename.find("ai_") == 0) prefix_dist["AI"]++;
            else if (filename.find("human_") == 0) prefix_dist["Human"]++;
            else prefix_dist["Other"]++;
        } catch (const std::exception& e) {
            ADD_FAILURE() << "Failed to analyze " << filename << ": " << e.what();
        }
    }
    
    std::cout << "\n=== Audio File Properties Summary ===" << std::endl;
    std::cout << "Total files: " << all_files.size() << std::endl;
    
    std::cout << "\nSample Rate Distribution:" << std::endl;
    for (const auto& [rate, count] : sample_rate_dist) {
        std::cout << "  " << yspeech::getSampleRateName(rate) << ": " << count << std::endl;
    }
    
    std::cout << "\nChannel Distribution:" << std::endl;
    for (const auto& [ch, count] : channel_dist) {
        std::cout << "  " << ch << " channel(s): " << count << std::endl;
    }
    
    std::cout << "\nFile Type Distribution:" << std::endl;
    for (const auto& [type, count] : prefix_dist) {
        std::cout << "  " << type << ": " << count << std::endl;
    }
    std::cout << "=====================================\n" << std::endl;
    
    EXPECT_EQ(sample_rate_dist.size(), 1) << "All files should have the same sample rate";
    EXPECT_EQ(channel_dist.size(), 1) << "All files should have the same channel count";
    
    if (!sample_rate_dist.empty()) {
        EXPECT_EQ(sample_rate_dist.begin()->first, yspeech::SampleRate::SR_16000)
            << "Expected 16kHz sample rate";
    }
}

TEST_F(TestRealAudio, FileSourceFrames) {
    auto all_files = get_all_audio_files();
    
    if (all_files.empty()) {
        GTEST_SKIP() << "Audio directory not found: " << AUDIO_DIR;
    }
    
    const auto& test_file = all_files[0];

    auto file_source = yspeech::FileSource(test_file.string(), "test_file", 1.0, false);

    yspeech::AudioFramePtr frame;
    int frame_count = 0;
    int sample_rate = 0;
    int channels = 0;
    while (file_source.next(frame) && frame) {
        frame_count++;
        sample_rate = frame->sample_rate;
        channels = frame->channels;
        if (frame->eos) {
            break;
        }
    }

    EXPECT_GT(frame_count, 0);
    EXPECT_GT(sample_rate, 0);
    EXPECT_GT(channels, 0);
}

TEST_F(TestRealAudio, ProcessingPerformance) {
    auto all_files = get_all_audio_files();
    
    if (all_files.empty()) {
        GTEST_SKIP() << "Audio directory not found: " << AUDIO_DIR;
    }
    
    const size_t test_file_count = std::min(size_t(10), all_files.size());
    
    auto start = std::chrono::high_resolution_clock::now();
    size_t total_bytes_processed = 0;
    
    for (size_t i = 0; i < test_file_count; ++i) {
        try {
            auto stream = yspeech::AudioFileStream(all_files[i].string());
            yspeech::Bytes buffer;
            buffer.resize(16384);
            auto audio_stream = yspeech::AudioStreamIface(std::move(stream));
            
            yspeech::Size bytes_read;
            do {
                bytes_read = audio_stream.read(buffer.data(), buffer.size());
                total_bytes_processed += bytes_read;
            } while (bytes_read > 0);
        } catch (const std::exception& e) {
            ADD_FAILURE() << "Failed to process " << all_files[i].filename().string() << ": " << e.what();
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double mb_processed = static_cast<double>(total_bytes_processed) / (1024 * 1024);
    double throughput = mb_processed / (duration.count() / 1000.0);
    
    std::cout << "\n=== Processing Performance ===" << std::endl;
    std::cout << "Files processed: " << test_file_count << std::endl;
    std::cout << "Total data: " << mb_processed << " MB" << std::endl;
    std::cout << "Time: " << duration.count() << " ms" << std::endl;
    std::cout << "Throughput: " << throughput << " MB/s" << std::endl;
    std::cout << "==============================\n" << std::endl;
    
    EXPECT_GT(total_bytes_processed, 0);
    EXPECT_LT(duration.count(), 5000) << "Processing should complete within 5 seconds";
}

TEST_F(TestRealAudio, FullFileReadIntegrity) {
    auto all_files = get_all_audio_files();
    
    if (all_files.empty()) {
        GTEST_SKIP() << "Audio directory not found: " << AUDIO_DIR;
    }
    
    const auto& test_file = all_files[0];
    
    auto stream = yspeech::AudioFileStream(test_file.string());
    int num_channels = stream.micNum();
    
    yspeech::Bytes buffer;
    buffer.resize(4096);
    auto audio_stream = yspeech::AudioStreamIface(std::move(stream));
    
    size_t total_read = 0;
    size_t read_calls = 0;
    yspeech::Size bytes_read;
    
    do {
        bytes_read = audio_stream.read(buffer.data(), buffer.size());
        total_read += bytes_read;
        read_calls++;
    } while (bytes_read > 0);
    
    EXPECT_GT(total_read, 0);
    EXPECT_GT(read_calls, 0);
    
    size_t expected_samples = total_read / sizeof(int16_t);
    size_t expected_frames = expected_samples / num_channels;
    
    std::cout << "\n=== File Read Integrity: " << test_file.filename().string() << " ===" << std::endl;
    std::cout << "Total bytes read: " << total_read << std::endl;
    std::cout << "Read calls: " << read_calls << std::endl;
    std::cout << "Total samples: " << expected_samples << std::endl;
    std::cout << "Total frames: " << expected_frames << std::endl;
    std::cout << "Duration (16kHz): " << (expected_frames / 16000.0) << " seconds" << std::endl;
    std::cout << "========================================================\n" << std::endl;
}

TEST_F(TestRealAudio, SampleValueRange) {
    auto all_files = get_all_audio_files();
    
    if (all_files.empty()) {
        GTEST_SKIP() << "Audio directory not found: " << AUDIO_DIR;
    }
    
    const size_t test_count = std::min(size_t(5), all_files.size());
    
    for (size_t i = 0; i < test_count; ++i) {
        auto stream = yspeech::AudioFileStream(all_files[i].string());
        
        yspeech::Bytes buffer;
        buffer.resize(8192);
        auto audio_stream = yspeech::AudioStreamIface(std::move(stream));
        
        yspeech::Size bytes_read = audio_stream.read(buffer.data(), buffer.size());
        ASSERT_GT(bytes_read, 0);
        
        const int16_t* samples = reinterpret_cast<const int16_t*>(buffer.data());
        size_t num_samples = bytes_read / sizeof(int16_t);
        
        int16_t min_sample = INT16_MAX;
        int16_t max_sample = INT16_MIN;
        
        for (size_t j = 0; j < num_samples; ++j) {
            min_sample = std::min(min_sample, samples[j]);
            max_sample = std::max(max_sample, samples[j]);
        }
        
        EXPECT_GE(min_sample, INT16_MIN);
        EXPECT_LE(max_sample, INT16_MAX);
        
        if (i == 0) {
            std::cout << "\n=== Sample Value Range: " << all_files[i].filename().string() << " ===" << std::endl;
            std::cout << "Min sample: " << min_sample << std::endl;
            std::cout << "Max sample: " << max_sample << std::endl;
            std::cout << "========================================================\n" << std::endl;
        }
    }
}
