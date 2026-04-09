# 架构设计

## 核心结构

```mermaid
flowchart LR
    engine[Engine]
    runtime[EngineRuntime]
    source[IFrameSource]
    store[StreamStore / FrameRing]
    manager[PipelineManager]
    stage[PipelineStage]
    op[Operator]
    ctx[Context]

    engine --> runtime
    runtime --> source
    source --> store
    runtime --> manager
    manager --> stage
    stage --> op
    op <--> ctx
```

## 关键关系

- `Engine` 是对外 API。
- `EngineRuntime` 是内部编排器。
- `PipelineManager` 管 stage。
- `PipelineStage` 管 operator DAG。
- `StreamStore` 负责 `AudioFrame` 流通。
- `Context` 负责中间结果、事件、错误和统计。

## 真实执行路径

```mermaid
sequenceDiagram
    participant U as User
    participant E as Engine
    participant R as EngineRuntime
    participant S as Source
    participant P as PipelineManager
    participant C as Context

    U->>E: start()
    E->>R: start()
    R->>S: 读取或接收 10ms AudioFrame
    R->>C: 写入 audio_frame_* 元数据
    R->>P: run_stream(ctx, store, false)
    P->>C: operator 写入 asr_events / vad_segments
    R->>E: 派发 EngineEvent / status
    U->>E: finish()
    E->>R: finish()
    R->>P: run_stream(ctx, store, true)
    U->>E: stop()
```

## 设计要点

1. 输入源在 pipeline 之外。
2. 最小数据单位是 `shared_ptr<const AudioFrame>`。
3. 执行模型是“外层 source/event 线程 + stage 内 Taskflow”。
4. 流式入口是 `run_stream(ctx, store, flush)`。
5. stage 间的 `input/output.key` 目前更多是配置元数据，不是完整的数据总线。

## 模式语义

- `mode=offline` 会把文件 source 的实际 `playback_rate` 强制改成 `0.0`
- `source.type=file` 使用 `FileSource + AudioFramePipelineSource`
- `source.type=microphone` 和 `source.type=stream` 目前都会回退到默认 `MicSource`

详细设计说明见 [design.md](/Users/eagle/workspace/Playground/Yspeech/doc/design.md)。
