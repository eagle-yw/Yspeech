# 配置说明

## 概览

Yspeech 的配置建议分成“真实运行时字段”和“pipeline 构图字段”两层理解：

- 运行时层：`mode`、`task`、`log_level`、`frame`、`stream`、`runtime`
- 管道层：`global`、`pipelines`

`EngineRuntime` 会先处理运行时字段，再通过 `PipelineConfig` 把 `pipelines` 构成执行图。

## 最小示例

```json
{
  "name": "Streaming ASR",
  "version": "1.0",
  "mode": "streaming",
  "log_level": "info",
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
      "id": "source_stage",
      "ops": [
        {
          "id": "source",
          "name": "FileSource",
          "params": {
            "path": "__AUDIO_PATH__",
            "playback_rate": 20.0
          }
        }
      ]
    },
    {
      "id": "asr_stage",
      "depends_on": ["source_stage"],
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
| `frame` | object | 否 | `AudioFrame` 相关参数 |
| `stream` | object | 否 | 流式 ring 配置 |
| `runtime` | object | 否 | 运行图并发和执行参数 |
| `global` | object | 否 | 变量替换和全局 capability |
| `pipelines` | array | 否 | 新版多 stage 配置 |

`pipelines` 是必需字段。

## source_stage

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | `FileSource`、`MicrophoneSource` 或 `StreamSource` |
| `params.path` | string | 文件路径，`name=FileSource` 时使用；示例配置通常写成 `__AUDIO_PATH__` 占位 |
| `params.playback_rate` | number | 文件播放倍率，`1.0` 为实时，`0.0` 关闭节流；`mode=offline` 时会被当作 `0.0` |
| `device` | string | 当前实现保留字段 |

说明：

- `source_stage.ops[0].name = FileSource` 会创建 `FileSource`
- `source_stage.ops[0].name = MicrophoneSource` 会使用默认 `MicSource`
- `source_stage.ops[0].name = StreamSource` 会使用独立的 `StreamSource`
- `EngineConfigOptions.audio_path` 会覆盖 `source_stage.ops[0].params.path`
- 顶层 `source` 只保留兼容旧配置的 fallback，不再是推荐写法

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

`global.properties` 用于变量替换，`global.capabilities` 会在 stage 构建时安装到每个处理节点，并和节点级 capability 一起按 `Pre/Post` 顺序执行。

| 字段 | 类型 | 说明 |
|------|------|------|
| `properties` | object | 变量池 |
| `capabilities` | array | 全局 capability 列表，每项包含 `name` 和可选 `params` |

说明：

- `Capability` 是配置驱动的节点扩展
- 它和 `Aspect` 一样都能挂在 `Stage -> Core` 边界
- 但推荐把“框架统一关注点”放到 `Aspect`，把“按配置启停的节点行为”放到 `Capability`

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
| `depends_on` | array | 否 | stage 级依赖关系，描述静态 DAG 边 |
| `join_policy` | string | 否 | join 节点策略，当前支持 `all_of` / `any_of` |
| `join_timeout_ms` | int | 否 | join 等待超时，当前仅作为 DAG 语义层的提前放行条件 |
| `input` | object | 否 | 输入元数据 |
| `output` | object | 否 | 输出元数据 |
| `ops` | array | 是 | stage 内处理节点列表 |

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
- 推荐显式声明 `source_stage`
- 如果没有显式声明 `source_stage`，运行时仍会根据旧顶层 `source` 兼容性地注入内部 `SourceStage`
- `depends_on` 会在启动期编译成静态 DAG，运行期结构不再变化
- 线性子路径由 `tf::Pipeline` 执行，`Branch/Join` 由 `PipelineExecutor` 处理

### join_policy

仅对多入边 stage 生效。

| 值 | 语义 |
|------|------|
| `all_of` | 等所有上游同 key 结果到齐后再继续 |
| `any_of` | 任一上游结果到达即可继续，后续同 key 默认不重复触发 |

### join_timeout_ms

- 仅对 `Join` 节点生效
- 单位毫秒
- `0` 表示第一次检查就允许超时放行
- 当前实现不会额外启动定时线程，而是在新的 join 贡献到达时检查是否超过超时阈值
- 这意味着它是“轻量的超时放行语义”，不是独立的调度器

示例：

```json
{
  "id": "merge_stage",
  "depends_on": ["vad_stage", "feature_stage"],
  "join_policy": "all_of",
  "ops": [
    { "id": "merge", "name": "JoinBarrier" }
  ]
}
```

## ops

每个处理节点的字段如下：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | string | 是 | 节点唯一标识 |
| `name` | string | 是 | Core 类型名，用于工厂创建 |
| `params` | object | 否 | 初始化参数 |
| `capabilities` | array | 否 | 节点级 capability 列表，会和 `global.capabilities` 合并后安装到当前 stage |
| `depends_on` | array | 否 | 依赖的上游节点 id |
| `parallel` | bool | 否 | 预留标记，当前调度未直接消费 |
| `error_handling` | object | 否 | 错误处理策略 |

说明：

- 流式 ASR 主线默认使用增量 decode
- `FeatureStage` 发布的 `delta_features` 表示“本次新增特征区间”
- 这些 delta 需要持续送入 core 的流式状态
- 不能因为本轮未达到 decode 门槛，就跳过 delta 的接收
- `decode_final` 必须基于 `full_context` 做最终收尾，不能简单复用 partial 路径

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

## Core 名称约束

当前注册表里可直接使用的 core 名称是：

- `SileroVad`
- `KaldiFbank`
- `AsrParaformer`
- `AsrSenseVoice`
- `AsrWhisper`

如果某个领域 stage 需要绑定处理 core，但配置里写了未注册的名字，会在 stage build 阶段报“未知 core 名称”这一类错误。

需要区分两类 `ops[].name`：

- 领域处理节点
  - 例如 `SileroVad`、`KaldiFbank`、`AsrParaformer`
  - 这些名字必须能在对应的 `VadCoreFactory`、`FeatureCoreFactory`、`AsrCoreFactory` 中找到
- 结构化 DAG 节点标签
  - 例如示例里的 `PassThroughSource`、`PassThroughBranch`、`JoinBarrier`
  - 这些名字主要用于表达静态 DAG 结构，不要求映射到某个领域 core

## Capability 配置约定

当前 capability 项推荐写成下面的结构：

```json
{
  "name": "StatusCapability",
  "params": {
    "phase": "pre",
    "status": "before feature stage"
  }
}
```

其中：

- `name`
  - capability 注册名
- `params.phase`
  - 可选，当前支持 `pre` / `post`
- 其他字段
  - 由具体 capability 自己解释

当前主线已接入 capability 执行链，但内置 capability 仍然比较少；如果需要扩展，优先把“按配置启停”的逻辑做成 capability，而不是再新增一类 aspect。

当前内置 capability：

- `AlertCapability`
  - 通过 `emit_alert` 发出一条告警
- `StatusCapability`
  - 通过 `emit_status` 发出一条状态消息
- `LogCapability`
  - 记录一条配置化日志
