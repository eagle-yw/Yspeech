# 配置说明

## 配置结构

```json
{
  "name": "Pipeline 名称",
  "version": "1.0",
  "global": {
    "properties": {
      "model_root": "/models/speech",
      "sample_rate": 16000,
      "log_level": "INFO"
    }
  },
  "pipelines": [
    {
      "id": "stage_id",
      "name": "阶段名称",
      "max_concurrency": 8,
      "input": {
        "key": "input_buffer",
        "chunk_size": 1600
      },
      "output": {
        "key": "output_buffer"
      },
      "ops": [
        {
          "id": "operator_id",
          "name": "Operator 名称",
          "params": {
            "param1": "value1"
          },
          "parallel": true,
          "depends_on": []
        }
      ]
    }
  ]
}
```

## 配置字段说明

### 顶层字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | Pipeline 名称 |
| `version` | string | 否 | 配置版本 |
| `global` | object | 否 | 全局配置 |
| `pipelines` | array | 是 | Pipeline 阶段数组 |

### global.properties

| 字段 | 类型 | 说明 |
|------|------|------|
| `model_root` | string | 模型根路径 |
| `sample_rate` | int | 采样率 |
| `log_level` | string | 日志级别 |

### pipelines[] 字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | string | 是 | 阶段唯一标识 |
| `name` | string | 否 | 阶段名称 |
| `max_concurrency` | int | 否 | 最大并发数（默认 8） |
| `input` | object | 否 | 输入配置 |
| `output` | object | 否 | 输出配置 |
| `ops` | array | 是 | Operator 列表 |

### ops[] 字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | string | 是 | Operator 唯一标识 |
| `name` | string | 是 | Operator 类型名称 |
| `params` | object | 否 | 参数配置 |
| `parallel` | boolean | 否 | 是否并行执行 |
| `depends_on` | array | 否 | 依赖的 Operator ID 列表 |

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
