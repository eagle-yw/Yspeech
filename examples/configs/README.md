# Yspeech 配置示例

本目录包含 Yspeech 的各种配置示例，展示不同使用场景。

## 配置文件说明

### 1. simple_asr.json - 简单离线 ASR

最简单的离线语音识别配置，适用于文件转录。

**特点**：
- 离线模式（offline）
- 单个 Pipeline
- 直接输出文本结果

**使用场景**：
- 会议录音转录
- 音频文件转文字
- 批量处理音频文件

```cpp
yspeech::Engine engine("simple_asr.json");
auto result = processor.process("audio.wav");
```

---

### 2. streaming_asr.json - 流式 ASR

实时流式语音识别配置，适用于麦克风输入。

**特点**：
- 流式模式（streaming）
- VAD + ASR 两级 Pipeline
- 回调输出结果

**使用场景**：
- 实时字幕
- 语音输入法
- 会议实时转录

```cpp
yspeech::Engine engine("streaming_asr.json");
processor.on_result([](const auto& result) {
    std::cout << result.text << std::endl;
});
processor.start();
// 推送音频...
```

---

### 3. two_level_asr.json - 两级 Pipeline

完整的两级 Pipeline 配置，展示高级用法。

**特点**：
- 两级 Pipeline（预处理 + 推理）
- 并行度独立控制
- JSON 输出，包含时间戳和置信度

**使用场景**：
- 高质量语音识别
- 需要精细控制的场景
- 多阶段处理

```cpp
yspeech::Engine engine("two_level_asr.json");
auto results = processor.process_file("meeting.wav");
```

---

### 4. vad_only.json - 仅 VAD 检测

仅使用 VAD 功能，检测语音片段。

**特点**：
- 仅 VAD Pipeline
- 检测语音开始/结束
- 回调输出片段信息

**使用场景**：
- 语音活动检测
- 音频分段
- 录音触发

```cpp
yspeech::Engine engine("vad_only.json");
processor.on_vad([](bool is_speech, int64_t start_ms, int64_t end_ms) {
    if (is_speech) {
        std::cout << "语音开始：" << start_ms << " ms" << std::endl;
    } else {
        std::cout << "语音结束：" << end_ms << " ms" << std::endl;
    }
});
```

---

## 配置字段说明

### 顶层字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | Pipeline 名称 |
| `version` | string | 否 | 配置版本 |
| `mode` | string | 是 | `offline` 或 `streaming` |
| `source` | object | 否 | 输入来源配置 |
| `frame` | object | 否 | 最小 AudioFrame 配置 |
| `stream` | object | 否 | 连续流 ring buffer 配置 |
| `pipeline` | object | 否 | 推送到现有 pipeline 的聚合策略 |
| `output` | object | 是 | 输出配置 |
| `pipelines` | array | 是 | Pipeline 阶段数组 |
| `global` | object | 否 | 全局配置 |

### source 字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | string | `file` 或 `microphone` 或 `stream` |
| `path` | string | 文件路径（type=file 时） |
| `playback_rate` | number | 文件播放倍率（type=file 时），`1`=实时，`2`=2 倍速 |
| `device` | string | 采集设备名或 ID（type=microphone 时，可选） |

### frame 字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `sample_rate` | int | 采样率 |
| `channels` | int | 声道数 |
| `dur_ms` | int | 最小帧时长，当前默认 10ms |

### pipeline 字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `push_chunk_samples` | int | 推送到 pipeline 的聚合样本数，16kHz 下 `1600 = 100ms`，离线可用 `-1` |

### stream 字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `ring_capacity_frames` | int | `AudioFrame` 连续流 ring 的容量，单位是 frame 数 |

当前引擎统一以 `AudioFrame` 作为音频流最小单元。像 `SileroVad`、`KaldiFbank`、`AsrWhisper` 这样的节点，建议在 operator 参数中显式配置 `input_frame_key: "audio_frames"`。

如果同一段 `AudioFrame` 流需要被多个节点重复消费，比如 `VAD` 消费后 `Fbank` 还要继续读取，那么每个节点会在内部维护各自的 reader cursor，不会互相抢占数据；当 reader 落后超过 ring 容量时，会触发 overrun 恢复。

### output 字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | string | `text` 或 `json` 或 `callback` |
| `file` | string | 输出文件路径 |
| `include_segments` | bool | 包含分段信息 |
| `include_timestamp` | bool | 包含时间戳 |
| `include_confidence` | bool | 包含置信度 |

### pipelines 字段

每个 Pipeline 阶段包含：

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | string | 阶段唯一标识 |
| `name` | string | 阶段名称 |
| `max_concurrency` | int | 最大并发数 |
| `input` | object | 输入配置 |
| `output` | object | 输出配置 |
| `ops` | array | Operator 列表 |

### ops 字段

每个 Operator 包含：

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | string | Operator 唯一标识 |
| `name` | string | Operator 类型名称 |
| `params` | object | 参数配置 |
| `parallel` | bool | 是否并行执行 |
| `depends_on` | array | 依赖的 Operator ID |

---

## 变量替换

配置支持变量替换：

```json
{
  "global": {
    "properties": {
      "model_root": "/models/speech"
    }
  },
  "pipelines": [
    {
      "ops": [
        {
          "params": {
            "model_path": "${model_root}/vad.onnx"
          }
        }
      ]
    }
  ]
}
```

变量替换优先级：
1. `global.properties` 中的值
2. 系统环境变量

---

## 更多示例

查看 `examples/` 目录获取更多使用示例代码。
