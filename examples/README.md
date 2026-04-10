# Yspeech 示例程序

本目录包含当前仓库里最值得参考的三个程序：

- `simple_transcribe`：最小离线转录
- `streaming_demo`：流式识别与 benchmark
- `transcribe_tool`：按结果列表输出的命令行工具

## 快速开始

### 编译项目

```bash
cd /Users/eagle/workspace/Playground/Yspeech
cmake -B build -G Ninja
cmake --build build
```

## 运行 Demo

### 1. 离线 ASR 转录

```bash
./build/examples/simple_transcribe \
  examples/configs/offline_paraformer_asr.json \
  <音频文件>
```

```bash
./build/examples/simple_transcribe \
  examples/configs/offline_sensevoice_asr.json \
  <音频文件>
```

### 2. 流式 ASR 识别

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_paraformer_asr.json \
  <音频文件> \
  0.0
```

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_sensevoice_asr.json \
  <音频文件> \
  1.0
```

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_paraformer_asr_dag.json \
  <音频文件> \
  0.0
```

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_paraformer_asr_dag_timeout.json \
  <音频文件> \
  0.0
```

benchmark：

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_paraformer_asr.json \
  <音频文件> \
  20 \
  --queue 0 \
  --benchmark 10
```

### 3. 转录工具

```bash
./build/examples/transcribe_tool \
  examples/configs/offline_paraformer_asr.json \
  <音频文件>
```

```bash
./build/examples/transcribe_tool \
  examples/configs/offline_sensevoice_asr.json \
  <音频文件> \
  --verbose
```

说明：

- 示例 JSON 里的 `FileSource` 路径默认是 `__AUDIO_PATH__`
- 实际音频文件请在命令行第二个参数传入，或在代码里通过 `EngineConfigOptions.audio_path` 覆盖

## 示例程序列表

## 1. `simple_transcribe.cpp`

最简单的文件转录示例，展示推荐的 `Engine` 生命周期：

- 构造
- 注册回调
- `start()`
- `finish()`
- 等待 `stream_drained`
- `stop()`

运行：

```bash
./build/examples/simple_transcribe <配置文件> <音频文件>
```

## 2. `streaming_demo.cpp`

流式识别示例，展示真实项目里更接近调试和 benchmark 的用法。

运行：

```bash
./build/examples/streaming_demo [配置文件] [音频文件] [播放倍率] --queue <0|1> --benchmark <N>
./build/examples/streaming_demo [配置文件] [音频文件] [播放倍率] --ep <cpu|coreml> [--ane-only <0|1>]
```

参数说明：

- `播放倍率`: `1.0` 为音频真实速度，`20` 为 20 倍速
- `--benchmark N`: 连续运行 `N` 次，输出统计
- `--queue <0|1>`: 显式设置 internal event queue
- `--ep <cpu|coreml>`: 覆盖算子的 `execution_provider`
- `--ane-only <0|1>`: 与 `--ep coreml` 搭配
- `--quiet`: 减少过程输出

配置触发行为：

- `streaming_paraformer_asr.json`: 单线流式 ASR 主线
- `streaming_paraformer_asr_dag.json`: 静态 DAG ASR 验证
- `streaming_paraformer_asr_dag_timeout.json`: 静态 DAG + timeout 验证
- `streaming_sensevoice_asr.json`: SenseVoice 线性流式变体
- `vad_only.json`: 只输出 VAD 事件，不产生 ASR 文本
- `offline_*.json`: `streaming_demo` 会直接拒绝，请改用 `simple_transcribe` 或 `transcribe_tool`

## 3. `transcribe_tool.cpp`

面向“列出识别结果”的命令行工具。

运行：

```bash
./build/examples/transcribe_tool <配置文件> <音频文件> [--verbose]
```

功能：

- 支持详细输出模式
- 显示分词信息
- 显示处理时间

说明：

- 当前代码里没有实现 `--json`

## 配置文件说明

推荐直接使用的配置：

- `offline_paraformer_asr.json`
- `offline_sensevoice_asr.json`
- `streaming_paraformer_asr.json`
- `streaming_sensevoice_asr.json`
- `streaming_paraformer_asr_dag.json`
- `streaming_paraformer_asr_dag_timeout.json`

可作为结构模板的配置：

- `two_level_asr.json`
- `vad_only.json`

详细配置说明请参考 [configs/README.md](configs/README.md)。
