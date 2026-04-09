# 配置说明

## 概览

Yspeech 的配置建议分成“真实运行时字段”和“pipeline 构图字段”两层理解：

- 运行时层：`mode`、`task`、`log_level`、`source`、`frame`、`stream`
- 管道层：`global`、`pipelines`

`EngineRuntime` 会先处理运行时字段，再通过 `PipelineConfig` 把 `pipelines` 构成执行图。

## 最小示例

```json
{
  "name": "Streaming ASR",
  "version": "1.0",
  "mode": "streaming",
  "log_level": "info",
  "source": {
    "type": "file",
    "path": "audio.wav",
    "playback_rate": 20.0
  },
  "frame": {
    "sample_rate": 16000,
    "channels": 1,
    "dur_ms": 10
  },
  "stream": {
    "ring_capacity_frames": 6000
  },
  "global": {
    "properties": {
      "model_root": "./models"
    }
  },
  "pipelines": [
    {
      "id": "asr_stage",
      "max_concurrency": 4,
      "ops": [
        {
          "id": "fbank",
          "name": "KaldiFbank",
          "params": {
            "input_frame_key": "audio_frames",
            "output_key": "fbank"
          }
        }
      ]
    }
  ]
}
```

## 顶层字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 否 | 配置名称 |
| `version` | string | 否 | 配置版本 |
| `mode` | string | 否 | `offline` 或 `streaming` |
| `task` | string | 否 | 写入 `EngineEvent.task`，默认 `asr` |
| `log_level` | string | 否 | `debug/info/warn/error/none` |
| `source` | object | 否 | 输入来源配置 |
| `frame` | object | 否 | `AudioFrame` 相关参数 |
| `stream` | object | 否 | 流式 ring 配置 |
| `global` | object | 否 | 变量替换和全局 capability |
| `pipelines` | array | 否 | 新版多 stage 配置 |

`pipelines` 是必需字段。

## source

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | string | `file`、`microphone` 或 `stream` |
| `path` | string | 文件路径，`type=file` 时必填 |
| `playback_rate` | number | 文件播放倍率，`1.0` 为实时，`0.0` 关闭节流；`mode=offline` 时会被当作 `0.0` |
| `device` | string | 当前实现保留字段 |

说明：

- `type=file` 会创建 `FileSource`
- `type=microphone` 和 `type=stream` 都会回落到默认 `MicSource`
- `EngineConfigOptions.audio_path` 会覆盖 `source.path`

## frame

| 字段 | 类型 | 默认值 | 说明 |
|------|------|------|------|
| `sample_rate` | int | 16000 | 采样率 |
| `channels` | int | 1 | 声道数 |
| `dur_ms` | int | 10 | 单帧时长 |

## stream

| 字段 | 类型 | 默认值 | 说明 |
|------|------|------|------|
| `ring_capacity_frames` | int | 6000 | `audio_frames` ring 容量 |

## global

`global.properties` 用于变量替换，`global.capabilities` 会在 stage 构建时尝试安装到每个 operator。

| 字段 | 类型 | 说明 |
|------|------|------|
| `properties` | object | 变量池 |
| `capabilities` | array | 全局 capability 列表，每项包含 `name` 和可选 `params` |

### 变量替换

字符串类型参数支持 `${var}` 递归替换，优先级如下：

1. `global.properties`
2. 系统环境变量

## pipelines

每个 stage 的字段如下：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | string | 是 | 阶段唯一标识 |
| `name` | string | 否 | 阶段名称 |
| `max_concurrency` | int | 否 | stage executor 线程数，默认 8 |
| `input` | object | 否 | 输入元数据 |
| `output` | object | 否 | 输出元数据 |
| `ops` | array | 是 | Operator 列表 |

`input` 常见字段：

| 字段 | 类型 | 说明 |
|------|------|------|
| `key` | string | 输入键名 |
| `chunk_size` | int | 输入块大小 |

`output` 常见字段：

| 字段 | 类型 | 说明 |
|------|------|------|
| `key` | string | 输出键名 |

说明：

- `input`/`output` 字段当前会被解析保存
- 但主执行链路没有基于这些字段建立完整 stage 间数据路由
- 它们更适合当作配置语义说明，而不是当前稳定功能承诺

## ops

每个 operator 的字段如下：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | string | 是 | Operator 唯一标识 |
| `name` | string | 是 | Operator 类型名，用于工厂创建 |
| `params` | object | 否 | 初始化参数 |
| `capabilities` | array | 否 | operator 级 capability 列表 |
| `depends_on` | array | 否 | 依赖的 operator id |
| `parallel` | bool | 否 | 预留标记，当前调度未直接消费 |
| `error_handling` | object | 否 | 错误处理策略 |

### error_handling

| 字段 | 类型 | 说明 |
|------|------|------|
| `strategy` | string | `skip`、`retry` 或默认 `fail` |
| `max_retries` | int | 重试次数上限 |
| `retry_delay_ms` | int | 重试间隔 |

## 当前需要特别注意的字段

下面这些字段在配置中经常出现，但当前代码并没有把它们做成稳定的“自动行为”：

| 字段 | 当前状态 |
|------|------|
| `pipeline.push_chunk_samples` | 已出现在示例配置中，但运行时未直接消费 |
| 顶层 `output` | 已出现在示例配置中，但 `Engine`/示例程序不会自动生成文本文件或 JSON 文件 |
| `pipelines[].input/output` | 已解析，但数据路由能力有限 |
| `ops[].parallel` | 已解析，但不直接驱动并发 |

## Operator 名称约束

当前注册表里可直接使用的 operator 名称是：

- `SileroVad`
- `KaldiFbank`
- `AsrParaformer`
- `AsrSenseVoice`
- `AsrWhisper`

如果配置里写了别的名字，会在 stage build 阶段报 `Unknown operator type`。
