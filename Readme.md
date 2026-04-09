# Yspeech

**Yspeech** 是一个基于 C++23 模块的现代化语音处理管道框架，当前以流式 `AudioFrame` 处理和配置驱动的 operator 编排为核心。

## 核心特性

- C++23 模块
- `AudioFrame` 流式处理
- `Taskflow` 驱动的 stage/operator 调度
- JSON 配置驱动
- `Engine` 统一事件、状态和性能回调
- 可扩展的 Operator / Capability / Aspect 体系

## 快速开始

### 1. 构建

```bash
cmake -B build -G Ninja
cmake --build build
```

### 2. 运行示例

```bash
./build/examples/simple_transcribe \
  examples/configs/offline_paraformer_asr.json \
  model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav
```

### 3. 运行测试

```bash
./build/test
```

## 文档

文档按两类整理，优先从这两份总览开始：

- 设计文档
  - [设计文档](doc/design.md) - 代码设计、运行链路、配置生效边界
  - [架构设计](doc/architecture.md) - 系统结构和时序
  - [核心组件](doc/components.md) - `Engine`、`EngineRuntime`、`PipelineManager` 等职责划分
  - [配置说明](doc/configuration.md) - 配置字段、实际生效项和限制
  - [性能说明](doc/performance.md) - 统计项、benchmark 用法和调优关注点
- 使用说明
  - [使用说明](doc/usage.md) - 构建、运行、生命周期、常见限制
  - [示例程序](examples/README.md) - `simple_transcribe`、`streaming_demo`、`transcribe_tool`
  - [示例配置](examples/configs/README.md) - 哪些配置可直接运行，哪些需要额外处理
  - [测试](doc/testing.md) - 测试命令与筛选方式
  - [故障排查](doc/troubleshooting.md) - 常见问题
  - [贡献指南](doc/contributing.md) - 仓库协作约定

## 最小使用示例

```cpp
import std;
import yspeech.engine;

int main() {
    yspeech::EngineConfigOptions options;
    options.audio_path = "audio.wav";
    options.playback_rate = 0.0;

    yspeech::Engine engine("examples/configs/offline_paraformer_asr.json", options);
    engine.on_event([](const yspeech::EngineEvent& event) {
        if (event.asr && event.kind == yspeech::EngineEventKind::ResultStreamFinal) {
            std::println("{}", event.asr->text);
        }
    });

    engine.start();
    engine.finish();
    engine.stop();
}
```

## 当前实现的几个重要事实

- 主执行入口是 `run_stream(ctx, store, flush)`
- 当前已注册 operator 只有 `SileroVad`、`KaldiFbank`、`AsrParaformer`、`AsrSenseVoice`、`AsrWhisper`
- 顶层 `output`、`pipeline.push_chunk_samples`、`ops[].parallel` 目前不应被理解为稳定自动行为

## 致谢

- Taskflow
- nlohmann/json
- ONNX Runtime
- kaldi-native-fbank
