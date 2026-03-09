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
- [文档](#文档)
- [使用示例](#使用示例)
- [许可证](#许可证)

## 👥 作者

**作者**: AI
**参与模型**: deepseek, glm, gemini, gpt-codex, minimax, qwen-3.5plus

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

## 文档

详细的设计开发文档请参考 `doc/` 目录：

- [架构设计](doc/architecture.md) - 核心架构和设计原则
- [配置说明](doc/configuration.md) - 配置文件结构和字段说明
- [核心组件](doc/components.md) - 主要组件和使用方法
- [构建系统](doc/build.md) - 构建选项和目标
- [性能优化](doc/performance.md) - 性能优化建议和基准测试
- [测试](doc/testing.md) - 测试方法和命令
- [故障排查](doc/troubleshooting.md) - 常见问题和解决方案
- [贡献指南](doc/contributing.md) - 如何贡献代码

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

### C++ 使用示例

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

## 许可证

本项目采用 MIT 许可证 - 查看 [LICENSE](LICENSE) 文件了解详情。

## 致谢

- [Taskflow](https://github.com/taskflow/taskflow) - 并行任务图框架
- [nlohmann/json](https://github.com/nlohmann/json) - JSON 库
- [ONNX Runtime](https://onnxruntime.ai/) - ML 推理引擎
