# 核心组件

## 1. Engine

`Engine` 是用户侧稳定入口，负责把内部 runtime 包装成易用 API。

```cpp
yspeech::EngineConfigOptions options;
options.audio_path = "audio.wav";
options.playback_rate = 0.0;

yspeech::Engine engine("config.json", options);
engine.on_event([](const yspeech::EngineEvent& event) {
    if (event.kind == yspeech::EngineEventKind::ResultSegmentFinal && event.asr.has_value()) {
        std::println("{}", event.asr->text);
    }
});
engine.start();
engine.finish();
engine.stop();
```

职责：

- 构造和持有 `EngineRuntime`
- 把 `AsrEvent`/VAD/状态统一转换成 `EngineEvent`
- 维护 internal event queue
- 暴露 `on_event`、`on_status`、`on_performance`、`on_alert`

说明：

- `enable_event_queue=false` 时，事件不会进入内部队列，但 callback 仍会执行
- `audio_path`、`playback_rate`、`log_level` 支持覆盖配置文件

## 2. EngineRuntime

`EngineRuntime` 是真正的运行时编排层。

职责：

- 加载配置并应用日志级别
- 创建 `StreamStore`、`PipelineManager`、`Context`
- 决定 `source.type` 对应的输入源实现
- 驱动 `run_stream(..., flush=false/true)`
- 汇总性能统计并发出 `input_eof` / `stream_drained`

## 3. PipelineManager

统一管理单级和多级 pipeline，真实入口只有流式接口：

```cpp
yspeech::PipelineManager manager;
manager.build("config.json");
manager.run_stream(ctx, store, false);
manager.run_stream(ctx, store, true);
```

职责：

- 根据 `PipelineConfig` 构建 stage
- 单 stage 直接执行，多 stage 为每个 stage 起线程
- stage 内部通过 `Taskflow` 跑 operator DAG

限制：

- `parallel` 不直接驱动调度
- `pipelines[].input/output` 当前不是完整的数据路由实现

## 4. StreamStore / FrameRing

流式数据面，负责 `AudioFrame` 通道：

```cpp
yspeech::StreamStore store;
store.init_audio_ring("audio_frames", 6000);
store.push_frame("audio_frames", frame);
auto read = store.read_frame("audio_frames", "vad_reader");
```

职责：

- 管理 `audio_frames` ring
- 支持多 reader 独立游标
- 在 reader 落后时支持 overrun 恢复
- 保留 `eos/gap` 语义

## 5. Context

运行时上下文和结果总线：

```cpp
yspeech::Context ctx;
ctx.set("asr_text", text);
ctx.set("vad_segments", segments);
auto results = ctx.get<std::vector<yspeech::AsrEvent>>("asr_events");
```

职责：

- 持有中间结果 KV
- 保存 `asr_events`、`vad_segments`
- 记录错误和性能统计

常见键：

- `asr_events`
- `vad_segments`
- `vad_is_speech`
- `global_eof`

## 6. Operator 系统

可插拔的 Operator 接口：

```cpp
struct MyOperator {
    void init(const json& config) {}

    bool ready(Context& ctx, StreamStore& store) {
        return true;
    }

    StreamProcessResult process_stream(Context& ctx, StreamStore& store) {
        return {};
    }

    StreamProcessResult flush(Context& ctx, StreamStore& store) {
        return {};
    }
};
```

当前已注册的 operator：

- `SileroVad`
- `KaldiFbank`
- `AsrParaformer`
- `AsrSenseVoice`
- `AsrWhisper`

接口特征：

- `init(config)` 必需
- `process_stream(ctx, store)` 必需
- `ready(ctx, store)` 可选
- `flush(ctx, store)` 可选

## 7. Capability 系统

Operator 能力扩展：

```cpp
op.install("TimerCapability");
op.install("LogCapability", {{"log_level", "DEBUG"}});
```

职责：

- 给 operator 注入前后置行为
- 支持 operator 级和 global 级 capability
- 从配置里的 `capabilities` 自动安装

## 8. Aspect 系统

AOP 切面编程：

```cpp
pipeline.add_aspect(yspeech::LoggerAspect{});
pipeline.add_aspect(yspeech::TimerAspect{});
```

当前 stage 默认会注入 `TimerAspect`。

详细配置边界见 [configuration.md](/Users/eagle/workspace/Playground/Yspeech/doc/configuration.md)。
