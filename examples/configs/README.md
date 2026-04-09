# Yspeech 配置示例

这里放的是仓库里当前建议参考的配置文件，已经按“默认主线 / DAG 验证 / 其他流式变体 / 模板参考”整理过。

## 默认主线

### 推荐先用

| 文件 | 说明 |
|------|------|
| `streaming_paraformer_asr.json` | 当前推荐的单线流式 ASR 主线 |
| `streaming_paraformer_asr_capabilities.json` | 单线流式 ASR + capability 示例 |
| `offline_paraformer_asr.json` | 离线 ParaFormer，中文识别 |
| `offline_sensevoice_asr.json` | 离线 SenseVoice，多语言识别 |

## 静态 DAG 验证

| 文件 | 说明 |
|------|------|
| `streaming_paraformer_asr_dag.json` | 流式 ParaFormer，静态 DAG + join 示例 |
| `streaming_paraformer_asr_dag_timeout.json` | 流式 ParaFormer，静态 DAG + join timeout 示例 |

## 其他流式变体

| 文件 | 说明 |
|------|------|
| `streaming_sensevoice_asr.json` | 流式 SenseVoice，线性流式变体 |

## 模板参考

| 文件 | 说明 |
|------|------|
| `two_level_asr.json` | 使用 `global.properties` 的两阶段 ParaFormer 配置 |
| `vad_only.json` | 仅 VAD 场景，已补齐模型变量 |

## 各配置关注点

### `offline_paraformer_asr.json`

- `task = asr`
- `mode = offline`
- `source.type = file`
- 单 stage：`KaldiFbank -> AsrParaformer`

### `offline_sensevoice_asr.json`

- `task = asr`
- `mode = offline`
- `source.type = file`
- 单 stage：`KaldiFbank -> AsrSenseVoice`

### `streaming_paraformer_asr.json`

- `task = asr`
- `mode = streaming`
- `source.type = file`
- 三段结构：`SileroVad -> KaldiFbank -> AsrParaformer`
- 当前推荐的单线流式 ASR 开发和回归基线

### `streaming_paraformer_asr_capabilities.json`

- 基于单线流式 ASR 主线
- 演示 `global.capabilities` 和 `ops[].capabilities`
- `global.capabilities`
  - 给所有 stage 注入 `StatusCapability`
- `feature_stage`
  - 额外挂一个 `StatusCapability`
- 适合验证 capability 执行链和配置写法

### `streaming_sensevoice_asr.json`

- `task = asr`
- `mode = streaming`
- `source.type = file`
- 默认已补齐 `source.path`
- 三段结构：`SileroVad -> KaldiFbank -> AsrSenseVoice`
- 对应 SenseVoice 线性流式入口

### `streaming_paraformer_asr_dag.json`

- 使用静态 DAG：`capture -> vad -> feature`，同时保留 `vad -> merge` 支路
- `merge_stage` 使用 `join_policy = all_of`
- `Vad / Feature / ASR` 参数沿用当前 Taskflow 主线口径
- `asr_stage` 在 join 后继续处理
- 用来验证 `RuntimeDagExecutor + tf::Pipeline` 的组合路径

### `streaming_paraformer_asr_dag_timeout.json`

- 基于静态 DAG + join 示例
- `merge_stage` 增加 `join_timeout_ms = 0`
- `Vad / Feature / ASR` 参数沿用当前 Taskflow 主线口径
- 用来验证 join 的轻量超时放行语义

### `two_level_asr.json`

- `task = asr`
- 演示 `global.properties` 变量替换
- 保持两阶段结构
- 使用当前已注册 core 名称，可直接作为运行模板改造

### `vad_only.json`

- `task = vad`
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
  0.0
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
| `pipelines[].depends_on` | stage 级依赖关系 |
| `pipelines[].join_policy` | join 节点汇聚策略，当前支持 `all_of` / `any_of` |
| `pipelines[].join_timeout_ms` | join 节点等待超时，当前用于轻量超时放行 |
| `ops[].depends_on` | 当前 stage 内处理节点依赖 |
| `ops[].parallel` | 配置标记，当前调度不直接消费 |
| `ops[].error_handling` | 失败重试或跳过策略 |
| `global.properties` | 变量替换来源 |
| `global.capabilities` | 全局 capability，会安装到每个 stage |
| `ops[].capabilities` | 节点级 capability，会和全局 capability 合并 |

## 继续阅读

- [根目录说明](/Users/eagle/workspace/Playground/Yspeech/Readme.md)
- [配置说明](/Users/eagle/workspace/Playground/Yspeech/doc/configuration.md)
- [使用说明](/Users/eagle/workspace/Playground/Yspeech/doc/usage.md)
