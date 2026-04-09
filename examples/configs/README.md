# Yspeech 配置示例

这里放的是仓库里当前建议参考的配置文件，已经按现有实现整理过。

## 可直接使用的配置

### 离线转录

| 文件 | 说明 |
|------|------|
| `offline_paraformer_asr.json` | 离线 ParaFormer，中文识别 |
| `offline_sensevoice_asr.json` | 离线 SenseVoice，多语言识别 |

### 流式转录

| 文件 | 说明 |
|------|------|
| `streaming_paraformer_asr.json` | 流式 ParaFormer，包含 VAD + ASR |
| `streaming_sensevoice_asr.json` | 流式 SenseVoice，包含 VAD + ASR |

### 结构参考

| 文件 | 说明 |
|------|------|
| `two_level_asr.json` | 使用 `global.properties` 的两阶段 ParaFormer 配置 |
| `vad_only.json` | 仅 VAD 场景，已补齐模型变量 |

## 各配置关注点

### `offline_paraformer_asr.json`

- `mode = offline`
- `source.type = file`
- 单 stage：`KaldiFbank -> AsrParaformer`

### `offline_sensevoice_asr.json`

- `mode = offline`
- `source.type = file`
- 单 stage：`KaldiFbank -> AsrSenseVoice`

### `streaming_paraformer_asr.json`

- `mode = streaming`
- `source.type = file`
- `source.playback_rate = 20.0`
- 两个 stage：`SileroVad` + `KaldiFbank -> AsrParaformer`

### `streaming_sensevoice_asr.json`

- `mode = streaming`
- `source.type = file`
- 两个 stage：`SileroVad` + `KaldiFbank -> AsrSenseVoice`

### `two_level_asr.json`

- 演示 `global.properties` 变量替换
- 保持两阶段结构
- 使用当前已注册 operator，可直接作为运行模板改造

### `vad_only.json`

- `mode = streaming`
- `source.type = microphone`
- 单 stage：`SileroVad`

## 使用方式

```bash
./build/examples/simple_transcribe \
  examples/configs/offline_paraformer_asr.json \
  model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav
```

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_paraformer_asr.json \
  model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav \
  1.0
```

```bash
./build/examples/transcribe_tool \
  examples/configs/two_level_asr.json \
  model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav \
  --verbose
```

## 字段速查

| 字段 | 说明 |
|------|------|
| `source.path` | 文件输入路径 |
| `source.playback_rate` | 文件播放倍率 |
| `frame.sample_rate` | 采样率 |
| `frame.dur_ms` | 单帧时长 |
| `stream.ring_capacity_frames` | ring 容量 |
| `pipelines[].max_concurrency` | stage 并发度 |
| `ops[].depends_on` | operator 依赖 |
| `ops[].parallel` | 配置标记，当前调度不直接消费 |
| `ops[].error_handling` | 失败重试或跳过策略 |
| `global.properties` | 变量替换来源 |

## 继续阅读

- [根目录说明](/Users/eagle/workspace/Playground/Yspeech/Readme.md)
- [配置说明](/Users/eagle/workspace/Playground/Yspeech/doc/configuration.md)
- [使用说明](/Users/eagle/workspace/Playground/Yspeech/doc/usage.md)
