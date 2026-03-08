# Yspeech 示例程序

本目录包含 Yspeech 极简 API 的示例程序。

## 快速开始

### 编译项目

```bash
cd /Users/eagle/workspace/Playground/Yspeech
cmake -B build -G Ninja
cmake --build build
```

### 运行 Demo

#### 1. 离线 ASR 转录

```bash
./build/examples/simple_transcribe examples/configs/simple_asr.json examples/model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav
```

**输出示例:**
```
=== 转录结果 ===
文本：对我做了介绍啊那么我想说的是呢大家如果对我的研究感兴趣呢嗯
置信度：0.90
语言：zh
处理时间：474ms
```

#### 2. 流式 ASR 识别

```bash
./build/examples/streaming_demo examples/configs/streaming_asr.json examples/model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav
```

**输出示例:**
```
=== Yspeech 流式 ASR 实际音频测试 ===

配置文件: examples/configs/streaming_asr.json
音频文件: examples/model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav

加载音频: 89834 样本, 1 通道, 16000Hz
开始流式识别...
分块推送音频: 57 块, 每块 1600 样本 (100ms)

[识别结果 #1] 对我做了介绍啊
  置信度: 0.9
  语言: zh

...

=== 统计信息 ===
处理时间: 1226ms
音频块数: 57
结果数: 40
音频时长: 5614.62ms
RTF: 0.218358
```

### 编译单个示例

```bash
cmake --build build --target simple_transcribe
cmake --build build --target streaming_demo
cmake --build build --target transcribe_tool
```

## 示例程序列表

### 1. simple_transcribe.cpp
最简单的文件转录示例，展示如何使用一行代码完成转录。

**编译:**
```bash
cmake --build build --target simple_transcribe
```

**运行:**
```bash
./build/examples/simple_transcribe examples/configs/simple_asr.json
```

**代码示例:**
```cpp
#include <yspeech/speech_processor>

int main() {
    auto result = yspeech::transcribe("config.json", "audio.wav");
    std::println("识别结果：{}", result.text);
    return 0;
}
```

### 2. streaming_demo.cpp
流式识别示例，展示如何使用流式 API 进行实时识别。

**编译:**
```bash
cmake --build build --target streaming_demo
```

**运行:**
```bash
./build/examples/streaming_demo examples/configs/streaming_asr.json
```

**代码示例:**
```cpp
yspeech::SpeechProcessor processor("config.json");

processor.on_result([](const auto& result) {
    std::println("识别结果：{}", result.text);
});

processor.start();

while (true) {
    std::vector<float> audio = get_audio_chunk();
    processor.push_audio(audio);
    
    if (processor.has_result()) {
        auto result = processor.get_result();
        std::println("{}", result.text);
    }
}

processor.stop();
```

### 3. transcribe_tool.cpp
功能完整的转录工具，支持多种输出格式和选项。

**编译:**
```bash
cmake --build build --target transcribe_tool
```

**运行:**
```bash
./build/examples/transcribe_tool config.json audio.wav --verbose
./build/examples/transcribe_tool config.json audio.wav --json
```

**功能:**
- 支持详细输出模式
- 支持 JSON 格式输出
- 显示分词信息
- 统计处理时间

## 配置文件说明

示例配置文件位于 `examples/configs/` 目录：

- **simple_asr.json** - 最简单的离线 ASR 配置
- **streaming_asr.json** - 流式 ASR 配置（麦克风输入）
- **two_level_asr.json** - 两级 Pipeline 配置
- **vad_only.json** - 仅 VAD 功能配置

详细配置说明请参考 [configs/README.md](configs/README.md)。

## API 使用指南

### 1. 文件转录（离线模式）

```cpp
// 方法 1: 一行代码完成转录
auto result = yspeech::transcribe("config.json", "audio.wav");

// 方法 2: 创建处理器对象
yspeech::SpeechProcessor processor("config.json");
auto results = processor.process_file("audio.wav");
```

### 2. 流式识别（在线模式）

```cpp
yspeech::SpeechProcessor processor("config.json");

// 设置回调
processor.on_result([](const yspeech::AsrResult& result) {
    std::println("识别结果：{}", result.text);
});

processor.on_vad([](bool is_speech, int64_t start_ms, int64_t end_ms) {
    if (is_speech) {
        std::println("语音开始：{}ms", start_ms);
    } else {
        std::println("语音结束：{}ms - {}ms", start_ms, end_ms);
    }
});

// 启动流式处理
processor.start();

// 推送音频数据
while (recording) {
    std::vector<float> audio = get_audio_chunk();
    processor.push_audio(audio);
    
    // 获取识别结果
    while (processor.has_result()) {
        auto result = processor.get_result();
        handle_result(result);
    }
}

// 停止处理
processor.stop();

// 获取统计信息
auto stats = processor.get_stats();
std::println("{}", stats.to_string());
```

### 3. 高级用法

```cpp
yspeech::SpeechProcessor processor("config.json");

// 设置多个回调
processor.on_result([](const auto& result) {
    // 处理识别结果
});

processor.on_vad([](bool is_speech, int64_t start, int64_t end) {
    // 处理 VAD 事件
});

processor.on_status([](const std::string& status) {
    // 处理状态更新
});

// 状态查询
if (processor.is_speaking()) {
    float confidence = processor.get_confidence();
    std::println("置信度：{:.2f}", confidence);
}

// 统计信息
auto stats = processor.get_stats();
std::println("处理块数：{}", stats.audio_chunks_processed);
std::println("RTF: {:.2f}", stats.rtf);
```

## 数据结构

### AsrResult

```cpp
struct AsrResult {
    std::string text;                          // 识别文本
    float confidence = 0.0f;                   // 置信度
    float start_time_ms = 0.0f;               // 开始时间
    float end_time_ms = 0.0f;                 // 结束时间
    std::vector<WordInfo> words;              // 分词信息
    std::string language = "unknown";         // 语言
    std::string emotion;                      // 情感（SenseVoice）
};
```

### WordInfo

```cpp
struct WordInfo {
    std::string word;                         // 词语
    float start_time_ms = 0.0f;              // 开始时间
    float end_time_ms = 0.0f;                // 结束时间
    float confidence = 0.0f;                 // 置信度
};
```

### ProcessingStats

```cpp
struct ProcessingStats {
    size_t audio_chunks_processed = 0;       // 处理的音频块数
    size_t speech_segments_detected = 0;    // 检测到的语音片段数
    size_t asr_results_generated = 0;       // 生成的识别结果数
    double processing_time_ms = 0.0;        // 处理时间（毫秒）
    double rtf = 0.0;                       // 实时率
};
```

## 编译整个项目

```bash
cd /Users/eagle/workspace/Playground/Yspeech
cmake -B build -G Ninja
cmake --build build
```

编译完成后，示例程序位于 `build/examples/` 目录。

## 常见问题

### Q: 如何选择合适的配置文件？

A: 根据使用场景选择：
- **离线文件转录**: 使用 `simple_asr.json` 或 `two_level_asr.json`
- **实时麦克风输入**: 使用 `streaming_asr.json`
- **仅语音活动检测**: 使用 `vad_only.json`

### Q: 如何自定义配置？

A: 复制示例配置文件，修改以下关键字段：
- `name`: 配置名称
- `mode`: `offline` 或 `streaming`
- `pipelines`: 定义 Pipeline 阶段
- `ops`: 定义 Operator 及其参数

### Q: RTF（实时率）是什么意思？

A: RTF = 处理时间 / 音频时长。RTF < 1.0 表示处理速度快于实时，适合流式处理。

## 更多信息

- 项目文档：`README.md`
- 配置说明：`configs/README.md`
- C++ 模块使用：参考项目根目录的 `.trae/rules/`
