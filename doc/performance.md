# 性能说明

本文档只描述当前代码里真实能观测到的性能项，不再给出未经代码或基准支撑的固定提升数字。

## 可观测指标

`ProcessingStats` 当前会输出这些核心指标：

- `total_processing_time_ms`
- `operator_total_time_ms`
- `non_operator_time_ms`
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

### 5. operator 自身阈值

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
