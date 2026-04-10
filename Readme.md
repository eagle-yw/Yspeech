# Yspeech

**Yspeech** 是一个基于 C++23 模块的现代化语音处理管道框架，当前以流式 `AudioFrame` 处理、配置驱动的 stage/core 编排，以及 `Taskflow` 线性流水线执行为核心。

## 核心特性

- C++23 模块
- `AudioFrame` 流式处理
- `Taskflow` 驱动的 stage/core 调度
- JSON 配置驱动
- `Engine` 统一事件、状态和性能回调
- 可扩展的 Core / Capability / Aspect 体系

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
  <音频文件>
```

说明：

- 示例 JSON 里的 `FileSource` 路径默认使用 `__AUDIO_PATH__` 占位
- 推荐在运行时通过命令行参数或 `EngineConfigOptions.audio_path` 传入真实音频文件

### 3. 运行测试

```bash
./build/test
```

## 文档

文档按两类整理，优先从这两份总览开始：

- 仓库当前支持的示例配置统一放在 `examples/configs/`

- 设计文档
  - [设计文档](doc/design.md) - 代码设计、运行链路、配置生效边界
  - [推荐开发基线](doc/recommended-baseline.md) - 当前默认主线、推荐配置与回归测试
  - [开发者扩展指南](doc/developer-extension.md) - 新增 runtime/domain 能力时的目录与职责约定
  - [架构设计](doc/architecture.md) - 系统结构和时序
  - [核心组件](doc/components.md) - `Engine`、`EngineRuntime`、runtime DAG 与 stage/core 分层
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

### 使用内置 file source

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

### 使用 `StreamSource` 外部推帧

适合 SDK 集成、实时采集接入或自定义上游音频编排：

```cpp
import std;
import yspeech.engine;

int main() {
    yspeech::Engine engine("examples/configs/streaming_paraformer_asr_stream_source.json");

    engine.on_event([](const yspeech::EngineEvent& event) {
        if (event.asr && event.kind == yspeech::EngineEventKind::ResultStreamFinal) {
            std::println("{}", event.asr->text);
        }
    });

    engine.start();

    engine.push_frame(frame_0);
    engine.push_frame(frame_1);
    engine.push_frame(eos_frame);

    engine.stop();
}
```

说明：

- 显式 `source_stage.ops[0].name = "StreamSource"` 时，运行时会使用独立的 `StreamSource`
- 这种模式更适合作为程序集成入口
- `streaming_demo` 主要演示内置 `file/microphone` source，不是 `push_frame(...)` 的最佳入口

## 当前推荐事实

- 单线流式 ASR 的默认开发基线是 `examples/configs/streaming_paraformer_asr.json`
- 外部推帧模板是 `examples/configs/streaming_paraformer_asr_stream_source.json`
- `streaming_demo` 默认就使用这条单线 Taskflow 配置
- `EngineRuntime` 的流式运行时已经统一切到 Taskflow 主线
- 新运行时已支持“配置驱动、启动期构图、运行期静态 DAG”的模式
- 线性 stage 段由 `tf::Pipeline` 执行，`Branch/Join` 由 `RuntimeDagExecutor` 负责最小路由与汇聚语义
- 当前已注册 core 名称只有 `SileroVad`、`KaldiFbank`、`AsrParaformer`、`AsrSenseVoice`、`AsrWhisper`
- 示例配置里推荐显式声明 `source_stage`；顶层 `source` 仅保留旧配置兼容
- 顶层 `output`、`pipeline.push_chunk_samples`、`ops[].parallel` 目前不应被理解为稳定自动行为

## 推荐 Taskflow 示例

- 线性新运行时：
  [examples/configs/streaming_paraformer_asr.json](/Users/eagle/workspace/Playground/Yspeech/examples/configs/streaming_paraformer_asr.json)
- 外部推帧：
  [examples/configs/streaming_paraformer_asr_stream_source.json](/Users/eagle/workspace/Playground/Yspeech/examples/configs/streaming_paraformer_asr_stream_source.json)
- 静态 DAG + join：
  [examples/configs/streaming_paraformer_asr_dag.json](/Users/eagle/workspace/Playground/Yspeech/examples/configs/streaming_paraformer_asr_dag.json)
- 静态 DAG + join timeout：
  [examples/configs/streaming_paraformer_asr_dag_timeout.json](/Users/eagle/workspace/Playground/Yspeech/examples/configs/streaming_paraformer_asr_dag_timeout.json)

## 致谢

- Taskflow
- nlohmann/json
- ONNX Runtime
- kaldi-native-fbank
