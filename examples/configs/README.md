# Yspeech 配置示例

这里放的是仓库里当前建议参考的配置文件，已经按“默认主线 / DAG 验证 / 其他流式变体 / 模板参考”整理过。
根目录旧 `configs/` 已移除，示例配置统一以当前目录为准。
其中 `FileSource` 示例里的 `path` 默认使用 `__AUDIO_PATH__` 占位，实际音频文件请在运行时通过命令行参数或 `EngineConfigOptions.audio_path` 传入。

## 默认主线

### 推荐先用

| 文件 | 说明 |
|------|------|
| `streaming_paraformer_asr.json` | 当前推荐的单线流式 ASR 主线 |
| `streaming_paraformer_asr_incremental_decode.json` | 单线流式 ASR + 增量 decode 对照样例 |
| `streaming_paraformer_asr_stream_source.json` | 单线流式 ASR + `StreamSource` 外部推帧示例 |
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
- `source_stage.ops[0].name = FileSource`
- 单路径静态 DAG：`FileSource -> (KaldiFbank -> AsrParaformer)`

### `offline_sensevoice_asr.json`

- `task = asr`
- `mode = offline`
- `source_stage.ops[0].name = FileSource`
- 单路径静态 DAG：`FileSource -> (KaldiFbank -> AsrSenseVoice)`

### `streaming_paraformer_asr.json`

- `task = asr`
- `mode = streaming`
- `source_stage.ops[0].name = FileSource`
- 单路径静态 DAG：`FileSource -> SileroVad -> KaldiFbank -> AsrParaformer`
- 当前推荐的单线流式 ASR 开发和回归基线
- `asr_stage` 默认使用更保守的门控：
  - `min_new_feature_frames = 8`
  - `min_first_partial_feature_frames = 4`
- 当前这样更容易同时兼顾：
  - 最终转写正确性
  - 3 段输出稳定性
  - 较低 `RTF`

### `streaming_paraformer_asr_capabilities.json`

- 基于单线流式 ASR 主线
- 演示 `global.capabilities` 和 `ops[].capabilities`
- `global.capabilities`
  - 给所有 stage 注入 `StatusCapability`
- `feature_stage`
  - 额外挂一个 `StatusCapability`
- 适合验证 capability 执行链和配置写法

### `streaming_paraformer_asr_stream_source.json`

- 基于单线流式 ASR 主线
- `source_stage.ops[0].name = StreamSource`
- 显式 `source_stage` 使用 `StreamSource`
- 适合 `Engine::push_frame(...)` 外部推帧场景
- 运行时会创建独立的 `StreamSource`
- 更适合作为 SDK/集成模板，而不是直接给 `streaming_demo` 使用

### `streaming_paraformer_asr_incremental_decode.json`

- 基于单线流式 ASR 主线
- 用来对照验证当前默认的增量 decode 路径
- 参数组合与主线保持一致，适合作为结果/性能对照样例

### `streaming_sensevoice_asr.json`

- `task = asr`
- `mode = streaming`
- `source_stage.ops[0].name = FileSource`
- 单路径静态 DAG：`FileSource -> SileroVad -> KaldiFbank -> AsrSenseVoice`
- 对应 SenseVoice 线性流式入口

### `streaming_paraformer_asr_dag.json`

- 使用静态 DAG：`FileSource -> SileroVad -> KaldiFbank`，同时保留 `vad -> merge` 支路
- `merge_stage` 使用 `join_policy = all_of`
- `Vad / Feature / ASR` 参数沿用当前 Taskflow 主线口径
- `asr_stage` 在 join 后继续处理
- 用来验证 `RuntimeDagExecutor + tf::Pipeline` 的组合路径
- `asr_stage` 同样沿用主线的保守门控阈值

### `streaming_paraformer_asr_dag_timeout.json`

- 基于静态 DAG + join 示例
- `merge_stage` 增加 `join_timeout_ms = 0`
- `Vad / Feature / ASR` 参数沿用当前 Taskflow 主线口径
- 用来验证 join 的轻量超时放行语义

## 当前 ASR 增量说明

- `AsrStage` 已具备增量特征区间推进的基础设施
- 当前主线默认使用增量 decode
- 当前需要特别注意：
  - `KaldiFbankOutput.delta_features` 是“本次新产出的特征帧”
  - `AsrStage` 必须先把 delta 喂给 core，再决定是否 decode
  - `decode_final` 必须基于 `full_context` 做最终收尾，不能简单复用 partial 路径

### `two_level_asr.json`

- `task = asr`
- 演示 `global.properties` 变量替换
- 保持两阶段结构
- 使用当前已注册 core 名称，可直接作为运行模板改造

### `vad_only.json`

- `task = vad`
- `mode = streaming`
- `source_stage.ops[0].name = MicrophoneSource`
- 单路径静态 DAG：`MicrophoneSource -> SileroVad`

## 使用方式

```bash
./build/examples/simple_transcribe \
  examples/configs/offline_paraformer_asr.json \
  <音频文件>
```

```bash
./build/examples/streaming_demo \
  examples/configs/streaming_paraformer_asr.json \
  <音频文件> \
  0.0
```

```cpp
yspeech::Engine engine("examples/configs/streaming_paraformer_asr_stream_source.json");
engine.start();
engine.push_frame(frame);
engine.push_frame(eos_frame);
engine.stop();
```

```bash
./build/examples/transcribe_tool \
  examples/configs/two_level_asr.json \
  <音频文件> \
  --verbose
```

## 字段速查

| 字段 | 说明 |
|------|------|
| `source_stage.ops[0].params.path` | 文件输入路径，占位值建议在运行时覆盖 |
| `source_stage.ops[0].params.playback_rate` | 文件播放倍率 |
| `frame.sample_rate` | 采样率 |
| `frame.dur_ms` | 单帧时长 |
| `stream.ring_capacity_frames` | ring 容量 |
| `runtime.asr_core_pool_size` | ASR core 池大小，建议和流式 ASR 的并发目标保持一致 |
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
