# 核心组件

## 1. PipelineManager

统一管理单级和多级 Pipeline：

```cpp
yspeech::PipelineManager manager;
manager.build("config.json");
manager.run(ctx);
manager.run_stream(ctx, store, false);
```

**特性**：
- 自动检测单/多级模式
- 单级模式无线程开销
- 多级模式并行执行
- `Taskflow` 静态图执行后端
- 支持流式 `ready/process_stream/flush`

## 2. StreamStore / FrameRing

流式数据面，负责 `AudioFrame` 通道：

```cpp
yspeech::StreamStore store;
store.init_audio_ring("audio_frames", 6000);
store.push_frame("audio_frames", frame);
auto read = store.read_frame("audio_frames", "vad_reader");
```

**特性**：
- 多 reader 重复消费
- overrun 检测与恢复
- `eos/gap` 语义
- 固定容量 ring 存储

## 3. Context

运行时上下文和结果总线：

```cpp
yspeech::Context ctx;
ctx.set("asr_text", text);
ctx.set("vad_segments", segments);
auto results = ctx.get<std::vector<yspeech::AsrEvent>>("asr_events");
```

**特性**：
- 通用 KV 数据容器
- 结果事件存储
- 错误记录和统计

## 4. Operator 系统

可插拔的 Operator 接口：

```cpp
// 定义 Operator
struct MyOperator {
    void init(const json& config) {
        // 初始化
    }
    
    void process(Context& ctx) {
        // 离线处理
    }

    bool ready(Context& ctx, StreamStore& store) {
        return true;
    }

    StreamProcessResult process_stream(Context& ctx, StreamStore& store) {
        return {};
    }

    StreamProcessResult flush(Context& ctx, StreamStore& store) {
        return {};
    }
};
```

**特性**：
- C++20 Concepts 约束
- 类型擦除封装
- 自动注册
- Capability 支持
- 同时支持离线和流式节点契约

## 5. Capability 系统

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

## 6. Aspect 系统

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
