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

## Profiling 的两层结构

接下来性能观测会明确拆成两层：

1. `Core Performance`
2. `Core Phase Performance`

### 第一层：Core Performance

这一层继续由 `TimerAspect` 提供，回答：

- 哪个 core 最慢
- 它在整次任务里占多少
- 是调用太多，还是单次太慢

### 第二层：Core Phase Performance

这一层用于回答：

- 这个热点 core 的时间具体花在哪里

例如 `asr` 可以进一步拆成：

- `pack_partial`
- `run_partial`
- `decode_partial`
- `pack_final`
- `run_final`
- `decode_final`

这一层不是由 `Capability` 直接测出来的，而是：

- core 内部主动记录 phase 时间
- 汇总进统一的 `core_phase_timings`
- 再由 `streaming_demo`、导出模块或 capability 消费

### 为什么不直接让 Capability 负责 Profiling

因为 capability 只包在 `Stage -> Core` 前后，天然适合：

- 打印
- 上报
- 告警

但它看不到 core 内部的真实阶段边界，所以不适合直接承担默认 profiling 采样。

因此默认原则是：

- `Aspect` 负责边界总耗时
- `Core` 负责内部 phase 耗时
- `Capability` 负责消费这些 profiling 数据

当前主线里，这一层已经能直接回答：

- `asr` 的时间主要花在 `run`
- `pack` 和 `decode` 占比很小
- `run_final` 通常比 `run_partial` 更贵

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

## `Peak Memory` 的理解方式

`Peak Memory` 是任务运行期间进程峰值内存的观测值。

它更适合用来回答这两个问题：

- 当前主线有没有明显内存回退
- 某次结构调整有没有引入重复缓存或重复模型实例

它不适合用来直接推导：

- 某个单独 core 占了多少内存
- 某个字段精确占了多少 MB

### 当前主线里最常见的内存来源

对流式 ASR 主线来说，峰值内存通常主要来自：

- ASR 模型实例
- 流式特征快照
- 尚未回收的 closed segment
- 音频输入与事件队列的暂存

### 一次实际收敛记录

在 `streaming_paraformer_asr.json` 主线配置上，我们曾观察到：

- `Peak Memory ≈ 670 MB`

定位后发现，主要是 3 类结构性冗余叠加：

1. `AsrStage` 默认按 `pipeline_lines` 扩成多份 core
2. 同一份特征同时保存在
   - `RuntimeContext.stream_feature_snapshots`
   - `SegmentState.features_accumulated`
3. closed segment 没有及时清理大对象和回收

收敛后改成：

- ASR core 默认池大小为 `1`
- ASR 直接读取全流特征快照
- 不再把整份流特征复制进每个 segment
- closed segment 在完成后清理 `audio/features/result`
- stream final 后清理对应 stream 的特征快照

同一主线配置再次实跑后，峰值内存降到：

- `Peak Memory = 393 MB`

这类记录说明：

- `Peak Memory` 对发现“重复状态/重复实例”很有价值
- 一旦它突然抬高，优先检查是否出现了
  - 模型池扩容
  - 特征双份缓存
  - segment 未回收

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

### 6. `AsrParaformer.min_new_feature_frames`

这个参数主要控制：

- 后续 `partial decode` 的触发频率
- `run_partial` 的调用次数
- `RTF`

但它不应该孤立地看。调这个参数时，至少要同时观察：

- `First Partial`
- `First Final`
- `RTF`
- `Core Phase Performance` 中的：
  - `asr/run_partial`
  - `asr/run_final`

### 7. `AsrParaformer.min_first_partial_feature_frames`

这个参数更偏实时性：

- 主要影响首个 partial 的出现时机
- 不宜为了压 `RTF` 而盲目抬高

推荐做法是：

- 先固定 `min_first_partial_feature_frames`
- 再扫描 `min_new_feature_frames`
- 在“首字时延仍可接受”的前提下找平衡点

## 一次实际门槛扫描

在当前 `streaming_paraformer_asr.json` 主线配置上，我们固定：

- `min_first_partial_feature_frames = 10`

只扫描：

- `min_new_feature_frames = 12 / 16 / 20 / 24 / 28 / 32`

得到一组稳定结果：

| `min_new_feature_frames` | `First Partial` | `First Final` | `RTF` | `Results` | `run_partial` 次数 |
|------|------:|------:|------:|------:|------:|
| 12 | 16.30ms | 132.36ms | 0.142 | 14 | 10 |
| 16 | 16.30ms | 119.42ms | 0.113 | 12 | 8 |
| 20 | 19.75ms | 69.94ms | 0.092 | 8 | 4 |
| 24 | 19.61ms | 69.06ms | 0.097 | 8 | 4 |
| 28 | 20.95ms | 70.78ms | 0.094 | 8 | 4 |
| 32 | 16.51ms | 86.08ms | 0.066 | 6 | 2 |

### 如何解读

- `12 / 16`
  - `partial` 太密
  - `run_partial` 次数明显偏多
  - `RTF` 和 `First Final` 都变差
- `20 / 24 / 28`
  - 构成当前较稳定的平衡区
  - `run_partial = 4`
  - `Results = 8`
  - `RTF` 维持在 `0.09x`
- `32`
  - 吞吐最好
  - 但 `partial` 次数只剩 `2`
  - `Results` 掉到 `6`
  - 更像吞吐优先档，不适合作为默认流式档

### 当前推荐

如果目标是：

- 保持流式响应感
- 避免把系统调成“近似离线”
- 同时维持较好的 `RTF`

当前更推荐把：

- `min_new_feature_frames = 20`
- `min_first_partial_feature_frames = 10`

作为主线基线。

这组参数不是绝对最小 `RTF`，但在：

- `First Partial`
- `First Final`
- `RTF`
- `run_partial` 频率

之间给出了更均衡的结果。

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
