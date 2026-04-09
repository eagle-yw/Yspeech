# 使用说明

本文档面向“怎么把仓库跑起来”和“怎么正确使用 `Engine`”。

## 构建

```bash
cmake -B build -G Ninja
cmake --build build
```

前提：

- CMake 3.30+
- Clang/LLVM，且支持 C++23 modules
- Ninja

如果仓库没有 `3rd-lib/`，根目录 `CMakeLists.txt` 会自动把 `YSPEECH_BUILD_DEPS` 打开，从源码构建依赖。

## 推荐示例

### 离线转录

```bash
./build/examples/simple_transcribe \
  examples/configs/offline_paraformer_asr.json \
  model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav
```

### 流式识别

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_paraformer_asr.json \
  model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav \
  0.0
```

### 流式识别 + capability 示例

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_paraformer_asr_capabilities.json \
  model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav \
  0.0
```

### 外部推帧流式识别

`source.type=stream` 适合由应用自己推送 `AudioFrame`，不依赖内置文件或麦克风 source。

推荐配置：

- `examples/configs/streaming_paraformer_asr_stream_source.json`

示例：

```cpp
import yspeech.engine;

yspeech::Engine engine("examples/configs/streaming_paraformer_asr_stream_source.json");
engine.start();
engine.push_frame(frame);
engine.push_frame(eos_frame);
engine.stop();
```

说明：

- 这种模式下运行时会创建独立的 `StreamSource`
- 更适合作为 SDK 集成入口
- `streaming_demo` 默认使用内置 source 编排，不是演示 `source.type=stream` 的最佳入口

### Taskflow 静态 DAG 示例

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_paraformer_asr_dag.json \
  model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav \
  0.0
```

### Taskflow 静态 DAG + timeout 示例

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_paraformer_asr_dag_timeout.json \
  model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav \
  0.0
```

### 命令行转录工具

```bash
./build/examples/transcribe_tool \
  examples/configs/offline_sensevoice_asr.json \
  model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/test_wavs/zh.wav \
  --verbose
```

## Engine 生命周期

最稳妥的调用顺序与仓库示例保持一致：

1. 构造 `Engine`
2. 注册 `on_event` / `on_status`
3. `start()`
4. `finish()`
5. 等待 `stream_drained`，或至少等到 `input_eof`
6. `stop()`

示例：

```cpp
import std;
import yspeech.engine;

int main() {
    yspeech::EngineConfigOptions options;
    options.audio_path = "audio.wav";
    options.playback_rate = 0.0;

    yspeech::Engine engine("examples/configs/offline_paraformer_asr.json", options);

    std::mutex mutex;
    std::condition_variable cv;
    bool drained = false;

    engine.on_event([](const yspeech::EngineEvent& event) {
        if (event.asr && event.kind == yspeech::EngineEventKind::ResultStreamFinal) {
            std::println("{}", event.asr->text);
        }
    });

    engine.on_status([&](const std::string& status) {
        if (status == "stream_drained") {
            {
                std::lock_guard lock(mutex);
                drained = true;
            }
            cv.notify_all();
        }
    });

    engine.start();
    engine.finish();

    {
        std::unique_lock lock(mutex);
        cv.wait_for(lock, std::chrono::seconds(30), [&]() { return drained; });
    }

    engine.stop();
}
```

## 事件与状态

### `on_event`

`Engine` 会统一派发这些事件：

- `ResultPartial`
- `ResultSegmentFinal`
- `ResultStreamFinal`
- `VadStart`
- `VadEnd`
- `Status`
- `Alert`

常见用法：

- 流式实时字幕：监听 `ResultPartial`
- 分段结果：监听 `ResultSegmentFinal`
- 最终转录：监听 `ResultStreamFinal`

### `on_status`

当前使用中最重要的状态有：

- `started`
- `input_eof`
- `stream_drained`

其中：

- `input_eof` 表示输入侧已经结束
- `stream_drained` 表示 runtime 已完成 flush 和事件排空，适合作为结束等待条件

## 运行时覆盖

`EngineConfigOptions` 当前最实用的字段有：

| 字段 | 作用 |
|------|------|
| `audio_path` | 覆盖配置中的 `source.path`，并强制切成 `file` source |
| `playback_rate` | 覆盖 `source.playback_rate` |
| `log_level` | 覆盖顶层 `log_level` |
| `enable_event_queue` | 控制是否把事件同时写入 internal queue |

优先级：

```text
EngineConfigOptions > 配置文件同名字段
```

## 示例程序差异

### `simple_transcribe`

- 面向最小可用离线转录
- 只保留最后一个分段或流最终结果

### `streaming_demo`

- 面向流式观察和 benchmark
- 支持 `--queue`、`--benchmark`、`--ep`、`--ane-only`
- 会打印 `ProcessingStats`
- 性能表里看总耗时贡献时，优先看 `Core Performance` 的 `% Task`，不是直接用 `Total / Processing Time`
- 启动时会根据配置文件识别运行画像，并给出一致的模式提示

行为约定：

- `mode=streaming` 且带 ASR core 配置：允许运行
- 无显式多分支依赖：按单路径静态 DAG 主线处理
- 声明了 `pipelines[].depends_on`：按静态 DAG 路径处理
- `task=vad` 且只有 `SileroVad`：按 VAD-only 处理，只输出 VAD 事件
- `mode=offline`：直接报错，提示改用 `simple_transcribe` 或 `transcribe_tool`

### `transcribe_tool`

- 面向批量查看结果细节
- 当前只支持 `--verbose`
- 现有代码里没有实现 `--json`

## 配置选择建议

可直接优先使用这些配置：

- `examples/configs/offline_paraformer_asr.json`
- `examples/configs/offline_sensevoice_asr.json`
- `examples/configs/streaming_paraformer_asr.json`
- `examples/configs/streaming_paraformer_asr_stream_source.json`
- `examples/configs/streaming_paraformer_asr_capabilities.json`
- `examples/configs/streaming_sensevoice_asr.json`
- `examples/configs/streaming_paraformer_asr_dag.json`
- `examples/configs/streaming_paraformer_asr_dag_timeout.json`

其中默认推荐主线是：

- `examples/configs/streaming_paraformer_asr.json`

### 流式运行时

流式配置默认就走 Taskflow 运行时。`runtime` 段现在主要用于调优参数，例如：

```json
{
  "runtime": {
    "pipeline_lines": 2
  }
}
```

说明：

- `pipeline_lines` 控制 Taskflow pipeline line 数
- 单路径静态 DAG 会走单个 `PipelineExecutor`
- 声明了 `pipelines[].depends_on` 的 DAG 配置会走 `RuntimeDagExecutor`
- 当前已经支持 `Branch + Join + join_timeout_ms`

### 静态 DAG 配置要点

- `pipelines[].depends_on` 用来声明 stage 级依赖边
- 多个上游指向同一 stage 时，该 stage 会被识别为 `Join`
- `pipelines[].join_policy` 当前支持：
  - `all_of`
  - `any_of`
- `pipelines[].join_timeout_ms` 用于轻量超时放行
- 线性段仍交给 `tf::Pipeline` 执行，仓库自身只补 branch/join 语义

需要额外注意的配置：

- `examples/configs/vad_only.json`
  - 适合接入麦克风或外部推帧场景
- `examples/configs/two_level_asr.json`
  - 适合拿来做带 `global.properties` 的两阶段模板

## 测试

运行全部测试：

```bash
./build/test
```

列出测试名后再按过滤器执行更稳妥：

```bash
./build/test --gtest_list_tests
./build/test --gtest_filter=TestSileroVad.*
./build/test --gtest_filter=TestAsrRealAudio.*
```

## 常见问题

### 配置能通过校验，但跑起来没效果

优先检查是否用了当前未真正生效的字段，例如：

- 顶层 `output`
- `pipeline.push_chunk_samples`
- `ops[].parallel`

## 继续阅读

1. [设计文档](/Users/eagle/workspace/Playground/Yspeech/doc/design.md)
2. [示例程序说明](/Users/eagle/workspace/Playground/Yspeech/examples/README.md)
3. [示例配置说明](/Users/eagle/workspace/Playground/Yspeech/examples/configs/README.md)
