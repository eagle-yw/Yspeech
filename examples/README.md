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

**ParaFormer (中文):**
```bash
./build/examples/simple_transcribe \
    examples/configs/offline_paraformer_asr.json \
    model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav
```

**SenseVoice (多语言):**
```bash
./build/examples/simple_transcribe \
    examples/configs/offline_sensevoice_asr.json \
    model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/test_wavs/zh.wav
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

**ParaFormer (中文):**
```bash
./build/examples/streaming_demo \
    examples/configs/streaming_paraformer_asr.json \
    model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav
```

**SenseVoice (多语言):**
```bash
./build/examples/streaming_demo \
    examples/configs/streaming_sensevoice_asr.json \
    model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/test_wavs/zh.wav
```

**输出示例:**
```
=== Yspeech 流式 ASR 实际音频测试 ===

配置文件: examples/configs/streaming_paraformer_asr.json
音频文件: model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav

开始流式识别...
通过统一 FrameSource 编排推送 10ms AudioFrame...

[VAD] 语音开始: 0ms
[实时转写 #1] 对我做了介绍啊

...

最终转写：对我做了介绍啊那么我想说的是呢大家如果对我的研究感兴趣呢嗯

=== Performance Summary ===
Chunks: 562, Segments: 1, Results: ...
```

#### 3. 转录工具

```bash
# 基本用法
./build/examples/transcribe_tool \
    examples/configs/offline_paraformer_asr.json \
    model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav

# 详细输出
./build/examples/transcribe_tool \
    examples/configs/offline_sensevoice_asr.json \
    model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/test_wavs/zh.wav \
    --verbose

# JSON 格式输出
./build/examples/transcribe_tool \
    examples/configs/offline_paraformer_asr.json \
    model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav \
    --json
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
./build/examples/simple_transcribe <配置文件> <音频文件>
```

**代码示例:**
```cpp
import std;
import yspeech.engine;
import yspeech.frame_source;

int main(int argc, char* argv[]) {
    yspeech::Engine engine(argv[1]);
    auto file_source = std::make_shared<yspeech::FileSource>(argv[2], "offline", 1.0, false);
    auto pipeline_source = std::make_shared<yspeech::AudioFramePipelineSource>(file_source);
    engine.set_frame_source(pipeline_source);
    yspeech::AsrResult result;
    engine.on_event([&](const yspeech::EngineEvent& event) {
        if (event.asr && event.kind == yspeech::EngineEventKind::ResultStreamFinal) {
            result = *event.asr;
        }
    });
    engine.start();
    engine.finish();
    while (!engine.input_eof_reached()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    engine.stop();
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
./build/examples/streaming_demo <配置文件> <音频文件>
```

**代码示例:**
```cpp
import std;
import yspeech.engine;
import yspeech.frame_source;

yspeech::Engine engine("config.json");
auto file_source = std::make_shared<yspeech::FileSource>("audio.wav");
auto pipeline_source = std::make_shared<yspeech::AudioFramePipelineSource>(file_source);
engine.set_frame_source(pipeline_source);

engine.on_event([](const yspeech::EngineEvent& event) {
    if (event.asr && event.kind == yspeech::EngineEventKind::ResultPartial) {
        std::println("实时转写：{}", event.asr->text);
    }
});

engine.start();
engine.finish();

while (!engine.input_eof_reached()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

std::this_thread::sleep_for(std::chrono::seconds(2));
engine.stop();
```

### 3. transcribe_tool.cpp
功能完整的转录工具，支持多种输出格式和选项。

**编译:**
```bash
cmake --build build --target transcribe_tool
```

**运行:**
```bash
./build/examples/transcribe_tool <配置文件> <音频文件> [--verbose] [--json]
```

**功能:**
- 支持详细输出模式
- 支持 JSON 格式输出
- 显示分词信息
- 统计处理时间

## 配置文件说明

示例配置文件位于 `examples/configs/` 目录：

### 离线配置
| 文件 | 说明 | ASR 模型 |
|------|------|---------|
| `offline_paraformer_asr.json` | 离线 ParaFormer | 中文语音识别 |
| `offline_sensevoice_asr.json` | 离线 SenseVoice | 多语言 + 情感检测 |

### 流式配置
| 文件 | 说明 | ASR 模型 |
|------|------|---------|
| `streaming_paraformer_asr.json` | 流式 ParaFormer | 中文语音识别 |
| `streaming_sensevoice_asr.json` | 流式 SenseVoice | 多语言 + 情感检测 |

### 其他配置
| 文件 | 说明 |
|------|------|
| `two_level_asr.json` | 两级 Pipeline 配置 |
| `vad_only.json` | 仅 VAD 功能配置 |

详细配置说明请参考 [configs/README.md](configs/README.md)。

## 支持的 ASR 模型

### ParaFormer
- **语言**: 中文
- **特点**: 高精度中文语音识别
- **模型路径**: `model/asr/sherpa-onnx-paraformer-zh-2023-09-14/`

### SenseVoice
- **语言**: 中文、英语、粤语、日语、韩语（自动检测）
- **特点**: 多语言 + 情感检测 + 逆文本规范化
- **模型路径**: `model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/`

**SenseVoice 配置参数:**
```json
{
  "language": "auto",      // auto/zh/en/yue/ja/ko
  "use_itn": true,         // 逆文本规范化
  "detect_emotion": true   // 情感检测
}
```

## API 使用指南

### 1. 文件转录（离线模式）

```cpp
// 统一 Engine 接口（离线）
yspeech::Engine engine("config.json");
auto file_source = std::make_shared<yspeech::FileSource>("audio.wav", "offline", 1.0, false);
auto pipeline_source = std::make_shared<yspeech::AudioFramePipelineSource>(file_source);
engine.set_frame_source(pipeline_source);

engine.start();
engine.finish();
while (!engine.input_eof_reached()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
engine.stop();
```

### 2. 流式识别（在线模式）

```cpp
yspeech::Engine engine("config.json");

// 设置回调
engine.on_event([](const yspeech::EngineEvent& event) {
    if (event.kind == yspeech::EngineEventKind::ResultPartial && event.asr.has_value()) {
        std::println("识别结果：{}", event.asr->text);
    }
});

// 启动流式处理
engine.start();

// 推送音频数据
while (recording) {
    std::vector<float> audio = get_audio_chunk();
    engine.push_audio(audio);
}

// 显式结束输入并收尾
engine.finish();
engine.stop();

// 获取统计信息
auto stats = engine.get_stats();
std::println("{}", stats.to_string());
```

### 3. 高级用法

```cpp
yspeech::Engine engine("config.json");

// 设置多个回调
engine.on_event([](const yspeech::EngineEvent& event) {
    if (event.kind == yspeech::EngineEventKind::ResultSegmentFinal && event.asr.has_value()) {
        std::println("{}", event.asr->text);
    }
});
engine.on_status([](const std::string& status) { std::println("{}", status); });

// 状态查询
if (engine.input_eof_reached()) {
    float confidence = 0.0f;
    std::println("置信度：{:.2f}", confidence);
}

// 统计信息
auto stats = engine.get_stats();
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
- **离线文件转录**: 使用 `offline_paraformer_asr.json` 或 `offline_sensevoice_asr.json`
- **实时麦克风输入**: 使用 `streaming_paraformer_asr.json` 或 `streaming_sensevoice_asr.json`
- **仅语音活动检测**: 使用 `vad_only.json`

### Q: ParaFormer 和 SenseVoice 如何选择？

A: 
- **ParaFormer**: 中文语音识别，精度高，速度快
- **SenseVoice**: 多语言支持，带情感检测，适合国际化场景

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
