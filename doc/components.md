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
- 创建 source、runtime 执行器与事件线程
- 决定 `source.type` 对应的输入源实现
- 在线性配置下驱动 `PipelineExecutor`
- 在声明了 stage 依赖的配置下驱动 `RuntimeDagExecutor`
- 汇总性能统计并发出 `input_eof` / `stream_drained`

## 3. PipelineExecutor / RuntimeDagExecutor / EventStage

当前推荐主线由两层执行器组成：

```cpp
yspeech::PipelineExecutor linear_executor;
yspeech::RuntimeDagExecutor dag_executor;
```

职责：

- `PipelineExecutor` 负责线性 stage 路径，并把执行交给 `tf::Pipeline`
- `RuntimeDagExecutor` 负责静态 DAG 的 `Branch/Join` 路由
- `EventStage` 负责把 runtime 结果统一派发成 `EngineEvent`
- stage callback 由执行器绑定到 `SourceStage / VadStage / FeatureStage / AsrStage / EventStage`

限制：

- `parallel` 不直接驱动调度
- `RuntimeDagExecutor` 只补最小静态 DAG 语义，不替代 `Taskflow`
- `pipelines[].input/output` 当前不是完整的数据路由实现

## 4. 领域 Stage / Core

当前目录已经按领域聚合：

- `src/yspeech/domain/source/`
- `src/yspeech/domain/vad/`
- `src/yspeech/domain/feature/`
- `src/yspeech/domain/asr/`

每个领域下都可以同时看到两类主代码：

- `Stage`
  - 负责 token、segment、事件时机、运行时语义
- `Core`
  - 负责真实算法与内部状态机
当前对应关系：

- `SourceStage -> PassThroughSourceCore / FileSource / MicrophoneSource / StreamSource`
- `VadStage -> SileroVadCore`
- `FeatureStage -> KaldiFbankCore`
- `AsrStage -> ParaformerCore / SenseVoiceCore / WhisperCore`

这种组织方式比把所有 stage 全放在单独目录下更适合当前仓库，因为扩展轴主要是按 `source / vad / feature / asr` 领域生长。

同时，运行时骨架统一放在：

- `src/yspeech/runtime/`

其中包含：

- `PipelineExecutor`
- `RuntimeDagExecutor`
- `EventStage`
- `RuntimeContext`
- `PipelineToken`

## 5. SegmentState / RuntimeContext

当前主线的数据面是 `SegmentState`，共享运行状态通过 `RuntimeContext` 传递：

```cpp
yspeech::SegmentState segment;
segment.audio_accumulated = samples;
```

职责：

- `SegmentState` 保存段级音频、识别结果和必要的段级状态
- `RuntimeContext` 保存配置、事件/状态/性能回调和少量共享运行时状态
- `PipelineToken` 负责在 stage 间携带当前帧或段的路由信息

## 6. Core 注册系统

当前主线保留了“按名字注册、按配置创建”的扩展思路，但注册目标已经收敛到领域 core：

- `VadCoreFactory`
- `FeatureCoreFactory`
- `AsrCoreFactory`

当前已注册的 core 名称：

- `SileroVad`
- `KaldiFbank`
- `AsrParaformer`
- `AsrSenseVoice`
- `AsrWhisper`

运行时在构建 stage 时，会根据配置里的 `ops[].name` 选择对应 core。

## 8. Capability 系统

能力扩展：

```cpp
install("StatusCapability", {{"status", "feature_pre"}});
install("LogCapability", {{"message", "before feature stage"}});
```

职责：

- 给处理节点注入前后置行为
- 支持节点级和 global 级 capability
- 当前在 `Stage -> Core` 边界自动安装并执行
- 适合做“按配置启停”的节点扩展
- 不负责定义默认 Performance 统计口径

当前内置 capability：

- `AlertCapability`
  - 通过 `RuntimeContext.emit_alert` 发告警
- `StatusCapability`
  - 通过 `RuntimeContext.emit_status` 发状态
- `LogCapability`
  - 通过日志输出一条配置化消息

## 9. Aspect 系统

AOP 切面编程：

当前 `Stage` 默认会注入 `TimerAspect`，配置里声明 `LoggerAspect` 时会一起挂到 `Stage -> Core` 边界。

职责：

- 处理框架级横切关注点
- 统一覆盖 `Stage -> Core` 调用边界
- 适合做计时、tracing、统一日志、性能统计
- 当前 `streaming_demo` 的 Performance 数据主来源是 `TimerAspect`

## 10. Aspect 与 Capability 的边界

两者都会挂在 `Stage -> Core` 边界，所以都有横切能力，但职责不同：

- `Aspect`
  - 框架级 AOP
  - 适合默认启用或由运行时统一管理的行为
  - 例如：`TimerAspect`、统一 tracing、统一日志
- `Capability`
  - 配置驱动的节点扩展
  - 适合按配置启停、按节点细粒度安装的行为
  - 例如：某个节点额外发状态、打审计标记、发告警

推荐原则：

- 框架统一关注点优先放 `Aspect`
- 需要通过配置启停的节点扩展优先放 `Capability`
- 不要同时为同一件事提供一套 `Aspect` 和一套 `Capability`

详细配置边界见 [configuration.md](/Users/eagle/workspace/Playground/Yspeech/doc/configuration.md)。
