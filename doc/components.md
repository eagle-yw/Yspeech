# 核心组件

## 1. PipelineManager

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

## 2. StreamController

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

## 3. Context

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

## 4. Operator 系统

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
