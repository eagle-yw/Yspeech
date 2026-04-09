# 故障排查

## 1. 配置校验失败

典型错误：

```text
Config validation failed:
  - pipelines[0]: missing required field 'id'
```

排查方向：

- 检查是否缺少 `pipelines[].id`
- 检查 `ops` 是否是数组
- 检查 `id`、`name`、`depends_on` 的类型是否正确

## 2. 未知 core 或 capability 名称

典型错误：

```text
Failed to create ASR core: unknown core name: Xxx
```

先区分报错对象：

- 如果是领域处理节点
  - 需要检查 `ops[].name` 是否是已注册的 core 名称
- 如果是 capability
  - 需要检查 `capabilities[].name` 是否是已注册的 capability 名称

当前可直接使用的内置 core 名称包括：

- `SileroVad`
- `KaldiFbank`
- `AsrParaformer`
- `AsrSenseVoice`
- `AsrWhisper`

当前可直接使用的内置 capability 名称包括：

- `StatusCapability`
- `AlertCapability`
- `LogCapability`

说明：

- 像 `PassThroughSource`、`PassThroughBranch`、`JoinBarrier` 这种名字，在示例里主要用于表达静态 DAG 结构
- 它们不是领域 core 的同义词，不应该和 `SileroVad`、`AsrParaformer` 这类处理 core 混用

## 3. 配置字段写了，但运行结果没有变化

优先检查是否使用了当前未真正生效的字段：

- 顶层 `output`
- `pipeline.push_chunk_samples`
- `ops[].parallel`
- `pipelines[].input/output.key`

这些字段目前更多是被解析或保留，而不是完整的自动行为。

## 4. 模型路径变量未展开

如果配置用了 `${var}`，但运行时报找不到模型文件，请优先检查：

- `global.properties` 是否提供了该变量
- 环境变量里是否存在同名变量

## 5. 流式程序退出过早

建议的结束顺序：

1. `start()`
2. `finish()`
3. 等待 `stream_drained` 或至少 `input_eof`
4. `stop()`

如果 `finish()` 后立刻 `stop()`，可能会丢掉 flush 阶段的最终结果。

## 6. 文件输入速度不符合预期

排查：

- `mode=offline` 时，文件输入会强制按 `playback_rate=0.0` 处理
- 只有 `mode=streaming` 下，`source.playback_rate` 才会体现节流效果

## 日志级别

推荐使用顶层 `log_level`：

```json
{
  "log_level": "debug"
}
```

支持：

- `debug`
- `info`
- `warn`
- `error`
- `none`
