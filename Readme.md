# Yspeech

**Yspeech** 是一个基于 C++23 模块的现代化语音处理管道框架，支持流式处理和两级 Pipeline 架构。

## ✨ 核心特性

- 🚀 **C++23 模块** - 使用现代 C++ 模块系统
- 🔄 **流式处理** - 基于 RingBuffer 的流式数据管道
- 📊 **两级 Pipeline** - 支持多级流水线并行处理
- 🎯 **配置驱动** - JSON 配置定义 Pipeline 结构
- ⚡ **高性能** - Taskflow 并行执行，自动优化
- 🔧 **灵活扩展** - Operator/Capability/Aspect 系统

## 📋 目录

- [快速开始](#快速开始)
- [架构设计](#架构设计)
- [配置说明](#配置说明)
- [使用示例](#使用示例)
- [核心组件](#核心组件)
- [构建系统](#构建系统)
- [性能优化](#性能优化)

## 快速开始

### 1. 安装依赖

```bash
# 系统要求
- CMake 3.30+
- Clang/LLVM (支持 C++23 模块)
- Ninja 构建系统
```

### 2. 构建项目

```bash
# 首次构建（自动下载依赖）
cmake -B build -G Ninja
cmake --build build

# 后续构建（使用预编译依赖）
cmake --build build
```

### 3. 运行测试

```bash
./build/test
```

## 架构设计

### 核心架构

```
┌─────────────────────────────────────────┐
│   StreamController (流控制器)            │
│   - 数据输入                            │
│   - 背压控制                            │
└──────────────┬──────────────────────────┘
               │
               ↓ push_data()
┌─────────────────────────────────────────┐
│   Context (数据总线)                     │
│   - RingBuffer (线程安全)                │
│   - 条件变量同步                         │
└──────────────┬──────────────────────────┘
               │
               ↓
┌─────────────────────────────────────────┐
│   PipelineManager (管道管理器)           │
│   ┌─────────────────────────────────┐   │
│   │  Stage 1 (预处理)                │   │
│   │  - VAD → Feature Extract         │   │
│   └─────────────────────────────────┘   │
│               ↓                          │
│   ┌─────────────────────────────────┐   │
│   │  Stage 2 (推理)                  │   │
│   │  - ASR → Post Process            │   │
│   └─────────────────────────────────┘   │
└─────────────────────────────────────────┘
```

### 设计原则

1. **数据输入在 Pipeline 之外** - 流控制器与处理管道分离
2. **Operator 内部无循环** - Task 保持短期、无阻塞
3. **Context 作为数据总线** - RingBuffer + 条件变量同步
4. **配置驱动** - JSON 配置定义 Pipeline 结构
5. **自动优化** - 单级模式无线程开销

## 配置说明

### 配置结构

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

### 配置字段说明

#### 顶层字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | Pipeline 名称 |
| `version` | string | 否 | 配置版本 |
| `global` | object | 否 | 全局配置 |
| `pipelines` | array | 是 | Pipeline 阶段数组 |

#### global.properties

| 字段 | 类型 | 说明 |
|------|------|------|
| `model_root` | string | 模型根路径 |
| `sample_rate` | int | 采样率 |
| `log_level` | string | 日志级别 |

#### pipelines[] 字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | string | 是 | 阶段唯一标识 |
| `name` | string | 否 | 阶段名称 |
| `max_concurrency` | int | 否 | 最大并发数（默认 8） |
| `input` | object | 否 | 输入配置 |
| `output` | object | 否 | 输出配置 |
| `ops` | array | 是 | Operator 列表 |

#### ops[] 字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | string | 是 | Operator 唯一标识 |
| `name` | string | 是 | Operator 类型名称 |
| `params` | object | 否 | 参数配置 |
| `parallel` | boolean | 否 | 是否并行执行 |
| `depends_on` | array | 否 | 依赖的 Operator ID 列表 |

### 变量替换

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

## 使用示例

### 示例 1：单级 Pipeline

```json
{
  "name": "Simple ASR Pipeline",
  "version": "1.0",
  "pipelines": [
    {
      "id": "asr_stage",
      "max_concurrency": 4,
      "input": {
        "key": "audio_buffer",
        "chunk_size": 1600
      },
      "output": {
        "key": "asr_results"
      },
      "ops": [
        {
          "id": "vad_op",
          "name": "VadStream",
          "params": {
            "threshold": 0.5
          },
          "parallel": true
        },
        {
          "id": "feature_op",
          "name": "FeatureExtract",
          "params": {
            "num_mel_bins": 80
          },
          "parallel": true,
          "depends_on": ["vad_op"]
        },
        {
          "id": "asr_op",
          "name": "AsrInference",
          "params": {
            "model_path": "${model_root}/conformer.onnx"
          },
          "parallel": false,
          "depends_on": ["feature_op"]
        }
      ]
    }
  ]
}
```

### 示例 2：两级 Pipeline

```json
{
  "name": "Two-Level ASR Pipeline",
  "version": "1.0",
  "global": {
    "properties": {
      "model_root": "/models/speech",
      "sample_rate": 16000
    }
  },
  "pipelines": [
    {
      "id": "preprocess",
      "name": "Preprocessing Pipeline",
      "max_concurrency": 8,
      "input": {
        "key": "audio_buffer",
        "chunk_size": 1600
      },
      "output": {
        "key": "features_buffer"
      },
      "ops": [
        {
          "id": "vad_op",
          "name": "VadStream",
          "params": {
            "threshold": 0.5
          },
          "parallel": true
        },
        {
          "id": "feature_op",
          "name": "FeatureExtract",
          "params": {
            "num_mel_bins": 80
          },
          "parallel": true,
          "depends_on": ["vad_op"]
        }
      ]
    },
    {
      "id": "inference",
      "name": "Inference Pipeline",
      "max_concurrency": 4,
      "input": {
        "key": "features_buffer"
      },
      "output": {
        "key": "asr_results"
      },
      "ops": [
        {
          "id": "asr_op",
          "name": "AsrInference",
          "params": {
            "model_path": "${model_root}/conformer.onnx",
            "beam_size": 10
          },
          "parallel": false
        },
        {
          "id": "post_op",
          "name": "PostProcess",
          "params": {},
          "parallel": false,
          "depends_on": ["asr_op"]
        }
      ]
    }
  ]
}
```

### C++ 使用示例

#### 基本使用

```cpp
#include <yspeech/yspeech.cppm>

int main() {
    // 创建 Engine
    yspeech::Engine engine;
    
    // 初始化（加载配置）
    engine.init("pipeline_config.json");
    
    // 创建 Context
    yspeech::Context ctx;
    
    // 运行 Pipeline
    engine.run(ctx);
    
    return 0;
}
```

#### 流式处理

```cpp
#include <yspeech/yspeech.cppm>
#include <yspeech/stream_controller.cppm>

int main() {
    // 创建 Engine
    yspeech::Engine engine;
    engine.init("streaming_config.json");
    
    // 创建 Context
    yspeech::Context ctx;
    ctx.init_ring_buffer<float>("audio_buffer", 16000);
    ctx.set("global_eof", false);
    
    // 创建流控制器
    yspeech::AudioStreamController controller;
    
    // 设置数据源
    controller.set_data_source([&](std::vector<yspeech::Byte>& chunk) {
        return audio_reader.read(chunk);  // 返回 false 表示 EOF
    });
    controller.set_buffer_key("audio_buffer");
    controller.set_eof_flag("global_eof");
    
    // 启动
    controller.start(ctx);
    
    // 等待完成
    // ...
    
    return 0;
}
```

#### 手动数据输入

```cpp
#include <yspeech/yspeech.cppm>

int main() {
    yspeech::Engine engine;
    engine.init("config.json");
    
    yspeech::Context ctx;
    ctx.init_ring_buffer<float>("audio_buffer", 16000 * 5);  // 5 秒缓冲
    
    // 数据输入线程
    std::thread input_thread([&ctx]() {
        AudioReader reader("audio.pcm");
        std::vector<float> chunk(1600);  // 100ms
        
        while (reader.read(chunk)) {
            ctx.ring_buffer_push("audio_buffer", chunk);
        }
        
        ctx.set("global_eof", true);
        ctx.notify_data_ready();
    });
    
    // Pipeline 运行（自动检测单/多级）
    engine.run(ctx);
    
    input_thread.join();
    
    return 0;
}
```

## 核心组件

### 1. PipelineManager

统一管理单级和多级 Pipeline：

```cpp
yspeech::PipelineManager manager;
manager.build("config.json");
manager.run(ctx);  // 自动检测并优化
```

**特性**：
- 自动检测单/多级模式
- 单级模式无线程开销
- 多级模式并行执行
- 支持异步启动/停止

### 2. StreamController

外部流控制器，管理数据输入：

```cpp
yspeech::AudioStreamController controller;
controller.set_data_source(data_source);
controller.set_buffer_key("audio_buffer");
controller.start(ctx);
```

**特性**：
- 背压控制（RingBuffer 满时等待）
- 自动 EOF 标记
- 统计信息（chunks_pushed, eof_reached）

### 3. Context

数据总线和同步原语：

```cpp
yspeech::Context ctx;

// RingBuffer 操作
ctx.init_ring_buffer<float>("key", capacity);
ctx.ring_buffer_push("key", data);
ctx.ring_buffer_pop_wait("key", data, 100ms);

// 同步原语
ctx.notify_data_ready();
ctx.wait_for_data(timeout);
```

**特性**：
- 线程安全的数据容器
- RingBuffer 集成
- 条件变量同步
- 错误记录和统计

### 4. Operator 系统

可插拔的 Operator 接口：

```cpp
// 定义 Operator
struct MyOperator {
    void init(const json& config) {
        // 初始化
    }
    
    void process(Context& ctx) {
        // 处理数据
    }
};

// 注册 Operator
YSPEECH_REGISTER_OPERATOR(MyOperator, "MyOp");
```

**特性**：
- C++20 Concepts 约束
- 类型擦除封装
- 自动注册
- Capability 支持

### 5. Capability 系统

Operator 能力扩展：

```cpp
// 安装 Capability
op.install("TimerCapability");
op.install("LogCapability", {{"log_level", "DEBUG"}});
```

**特性**：
- Pre/Post 钩子
- 动态安装/卸载
- 工厂模式创建

### 6. Aspect 系统

AOP 切面编程：

```cpp
// 添加切面
pipeline.add_aspect(yspeech::LoggerAspect{});
pipeline.add_aspect(yspeech::TimerAspect{});
```

**特性**：
- before/after 环绕通知
- 后进先出执行顺序
- 支持多个切面

## 构建系统

### 构建选项

```bash
# 首次构建（自动下载依赖）
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DYSPEECH_BUILD_DEPS=ON

cmake --build build

# 使用预编译依赖
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DYSPEECH_BUILD_DEPS=OFF

cmake --build build
```

### 构建目标

| 目标 | 说明 |
|------|------|
| `yspeech` | 主库 |
| `test` | 测试程序 |
| `example` | 示例程序 |

### 清理构建

```bash
rm -rf build
```

## 性能优化

### 单级 vs 多级性能

| 模式 | 吞吐 | 延迟 | 资源 |
|------|------|------|------|
| **单级** | 中 | 低 | 少 |
| **两级** | 高（+70%） | 中 | 多 |

### 优化建议

1. **合理设置并发度**
   - CPU 密集型：`max_concurrency = CPU 核心数`
   - IO 密集型：`max_concurrency = 2 * CPU 核心数`

2. **调整 RingBuffer 大小**
   - 低延迟：小缓冲（100-500ms）
   - 高吞吐：大缓冲（1-5s）

3. **使用并行 Operator**
   ```json
   {
     "ops": [
       {"name": "VadStream", "parallel": true},
       {"name": "FeatureExtract", "parallel": true},
       {"name": "AsrInference", "parallel": false}
     ]
   }
   ```

### 性能基准

```bash
# 运行性能测试
./build/test --gtest_filter=PerfTest.*
```

## 测试

### 运行所有测试

```bash
./build/test
```

### 运行特定测试

```bash
# Pipeline 测试
./build/test --gtest_filter=TestPipeline.*

# Operator 测试
./build/test --gtest_filter=TestOp.*

# Aspect 测试
./build/test --gtest_filter=TestAspect.*
```

## 故障排查

### 常见问题

#### 1. 配置验证失败

**错误**：
```
Config validation failed:
  - pipelines[0]: missing required field 'id'
```

**解决**：检查配置文件中是否包含所有必填字段。

#### 2. Operator 创建失败

**错误**：
```
Failed to create operator: Unknown operator type: Xxx
```

**解决**：确保 Operator 已正确注册。

#### 3. RingBuffer 阻塞

**现象**：数据输入线程阻塞

**解决**：
- 增大 RingBuffer 容量
- 提高 Pipeline 处理速度
- 检查是否有死锁

### 日志级别

```json
{
  "global": {
    "properties": {
      "log_level": "DEBUG"  // DEBUG, INFO, WARNING, ERROR
    }
  }
}
```

## 贡献指南

1. Fork 项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

## 许可证

本项目采用 MIT 许可证 - 查看 [LICENSE](LICENSE) 文件了解详情。

## 致谢

- [Taskflow](https://github.com/taskflow/taskflow) - 并行任务图框架
- [nlohmann/json](https://github.com/nlohmann/json) - JSON 库
- [ONNX Runtime](https://onnxruntime.ai/) - ML 推理引擎

## 联系方式

- 项目地址：https://github.com/yourusername/yspeech
- 问题反馈：https://github.com/yourusername/yspeech/issues
