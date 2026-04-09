# 性能说明

本文档只描述当前代码里真实能观测到的性能项，不再给出未经代码或基准支撑的固定提升数字。

## 可观测指标

`ProcessingStats` 当前会输出这些核心指标：

- `total_processing_time_ms`
- `core_total_time_ms`
- `non_core_time_ms`
- `time_to_first_partial_ms`
- `time_to_first_final_ms`
- `drain_after_eof_ms`
- `rtf`
- `stop_*`
- `event_dispatch_*`

## 如何观察性能

最直接的方式是运行 `streaming_demo`：

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_paraformer_asr.json \
  model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav \
  20 \
  --benchmark 5
```

它会输出单次 summary 或 benchmark 汇总表。

## `Performance Summary` 的口径

`streaming_demo` 当前会输出两层性能信息：

1. 总览表 `Performance Summary`
2. 明细表 `Core Performance`

当前主统计链路：

- `Performance Summary`
- `Core Performance`
- `ProcessingStats`

都以 `Aspect` 为主来源，默认由 `TimerAspect` 在 `Stage -> Core` 边界统一采集。

`Capability` 不负责定义默认性能统计口径。运行时治理如果需要告警，优先复用引擎层已有告警链，而不是再让 capability 参与默认 Performance 统计。

### 总览表

`Performance Summary` 现在分成两块：

- `Core Metrics`
  - 面向回归、基线对比和日常结果判断
  - 优先看：
    - `Processing Time`
    - `First Partial`
    - `First Final`
    - `Drain After EOF`
    - `RTF`
    - `Core Share`
- `Diagnostic Metrics`
  - 面向排障、治理和定位尾部开销
  - 包括：
    - `Stop.*`
    - `EOF *`
    - `Event *`
    - `Engine Init Time`
    - `Peak Memory`
    - `Avg CPU`

这里最容易混淆的是这 4 个字段：

| 字段 | 含义 |
|------|------|
| `Processing Time` | 整次任务的 wall time，作为总分母 |
| `Core Total Time` | 所有 core 调用耗时的累计值 |
| `Core Active Time` | 所有 core 活跃区间合并后的 wall time |
| `Non-Core Wall Time` | 总 wall time 中不属于 core 活跃区间的部分 |
| `Core Share` | core 对整次任务 wall time 的总体占比 |

注意：

- `Core Total Time` 是累计值，在并发场景下可能大于 `Processing Time`
- `Core Share` 基于 `Core Active Time` 计算，不基于累计调用时间计算
- 因此判断“这次任务主要花在哪”时，不要直接拿单个 core 的 `Total` 去除 `Processing Time`

### 明细表

`Core Performance` 里的字段含义是：

| 字段 | 含义 |
|------|------|
| `Total` | 该 core 的累计执行时间 |
| `Avg` | 单次平均执行时间 |
| `Calls` | 被调用次数 |
| `Exec` | 有效执行次数 |
| `% Task` | 该 core 在整次任务 wall time 中的活跃占比 |

这里的 `% Task` 才是“看它在任务执行总耗时里占多少”的列。

理解时建议这样看：

- 看 `Core Share`：先判断任务总体是不是主要耗在 core 上
- 再看各 core 的 `% Task`：判断哪个 core 是主热点
- 再配合 `Total / Avg / Calls`：判断是“单次慢”还是“调用太多”

### 为什么 `% Task` 不一定加起来正好 100%

这是正常的，因为整次任务 wall time 里还可能包含：

- 非 core 的处理
- 线程切换与排队
- 无算子活跃的等待区间

因此：

- `Core Share` 反映 core 整体占比
- 每个 core 的 `% Task` 反映各自对总任务时间的贡献
- 它们加起来不要求正好等于 `100%`

## 导出字段口径

`PerformanceExporter` 导出的 JSON / CSV 现在和终端表格保持一致：

- `total_processing_time_ms`
  - 对应 `Processing Time`
- `core_total_time_ms`
  - 对应 `Core Total Time`
- `core_active_time_ms`
  - 对应 `Core Active Time`
- `core_time_percent`
  - 对应总览表里的 `Core Share`
- `core_timings[].total_time_ms`
  - core 累计执行时间
- `core_timings[].active_wall_time_ms`
  - core 在任务中的活跃 wall time
- `core_timings[].task_time_percent`
  - 对应终端 `Core Performance` 里的 `% Task`

## 调优重点

### 1. `max_concurrency`

- 控制 stage 内 `Taskflow executor` 的线程数
- 对 CPU 密集 stage，通常从 CPU 核数附近开始试
- 对轻量 stage，过高并发可能只会放大调度开销

### 2. `stream.ring_capacity_frames`

- reader 落后时过小会更容易 overrun
- 过大则增加内存占用和排空时延

### 3. `source.playback_rate`

- 影响文件输入节奏
- benchmark 时常用大于 `1.0` 的值
- `offline` 模式下会被强制视为 `0.0`

### 4. 事件队列

- `enable_event_queue=false` 可只走 callback，不再额外压入内部队列
- `streaming_demo --queue 0` 可直接做 A/B

### 5. core 自身阈值

这类参数往往比框架参数更直接影响延迟：

- `KaldiFbank.enable_accumulation`
- `KaldiFbank.min_accumulated_frames`
- `AsrParaformer.min_new_feature_frames`
- `AsrSenseVoice.min_new_feature_frames`

## 认识几个“看起来像性能参数”的字段

| 字段 | 说明 |
|------|------|
| `ops[].parallel` | 当前不会直接改变调度策略 |
| `pipeline.push_chunk_samples` | 当前运行时未直接消费 |

## 测试与基准

```bash
./build/test --gtest_filter=TestKaldiFbankPerformance.*
./build/test --gtest_filter=TestAsrRealAudio.*
```
