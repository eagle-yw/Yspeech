# 开发者扩展指南

本文档只回答一个问题：

`现在仓库已经整理成 runtime + domain 结构后，新增一个能力应该怎么落代码？`

## 目录原则

当前代码目录按两层组织：

- `src/yspeech/runtime/`
  - 放运行时骨架、静态 DAG 执行图、事件分发、token/segment 状态
- `src/yspeech/domain/`
  - 放领域实现
  - 当前已有：
    - `vad/`
    - `feature/`
    - `asr/`

一句话原则：

- `runtime` 解决“怎么调度”
- `domain` 解决“怎么处理”

## 新增一个领域能力时的最小结构

如果要新增一个领域，例如 `speaker`，建议直接按下面的结构建：

```text
src/yspeech/domain/speaker/
  base.cppm
  my_model.cppm
  stage.cppm
```

建议职责：

- `base.cppm`
  - 定义该领域的 `Base`、`CoreIface`、`CoreFactory`、`CoreRegistrar`
- `my_model.cppm`
  - 放一个或多个具体 `Core`
- `stage.cppm`
  - 放这个领域对应的 `Stage`

## Stage 和 Core 的边界

### Stage

`Stage` 负责运行时边界：

- 接收 `PipelineToken`
- 读取/写入 `RuntimeContext`
- 读取/写入 `SegmentRegistry`
- 决定事件时机
- 把配置映射成对应 `Core`

`Stage` 不应该承担：

- 大量模型细节
- 复杂算法状态机
- 大量和具体模型绑死的逻辑

### Core

`Core` 负责领域处理逻辑：

- 解析模型配置
- 持有领域内部状态
- 执行真正的推理/特征/VAD 逻辑
- 输出领域结果

`Core` 不应该承担：

- DAG 路由
- engine 事件分发
- runtime 线程与队列管理

## Aspect 和 Capability 的边界

新增横切能力时，先判断它应该落在哪一层。

### Aspect

适合：

- 框架级 AOP
- 默认启用或由运行时统一管理
- 统一覆盖 `Stage -> Core` 边界

典型例子：

- `TimerAspect`
- 统一 tracing
- 统一性能统计
- 统一调用日志

### Capability

适合：

- 配置驱动的节点扩展
- 需要按 stage / node 精细启停
- 需要通过配置传入参数

典型例子：

- 节点级状态上报
- 审计标记
- 可配置告警
- 调试开关

仓库内置示例：

- `AlertCapability`
- `StatusCapability`
- `LogCapability`

推荐原则：

- 框架统一关注点优先用 `Aspect`
- 按配置启停的行为优先用 `Capability`
- 不要同时为同一件事做一套 `Aspect` 和一套 `Capability`

## 注册式扩展

当前主线保留“按名字注册、按配置创建”的思路，但注册目标已经是 `Core`：

- `VadCoreFactory`
- `FeatureCoreFactory`
- `AsrCoreFactory`

新增一个 core 时，建议保持这个模式：

```cpp
export class MyAsrCore : public AsrBase, public AsrCoreIface {
public:
    void init(const nlohmann::json& config) override;
    auto infer(const std::vector<std::vector<float>>& features) -> AsrResult override;
    void deinit() override;
};

namespace {
AsrCoreRegistrar<MyAsrCore> registrar("AsrMyModel");
}
```

然后由 `Stage` 在运行时按配置里的 `ops[].name` 创建对应 core。

如果是 capability，则继续保留注册式扩展：

```cpp
export class MyCapability {
public:
    explicit MyCapability(const nlohmann::json& config = {}) { init(config); }
    void init(const nlohmann::json& config);
    void apply(yspeech::RuntimeContext& runtime);
    auto phase() const -> yspeech::CapabilityPhase;
};

namespace {
yspeech::CapabilityRegistrar<MyCapability> registrar("MyCapability");
}
```

## 配置层怎么接

当前配置 schema 仍然使用：

- `pipelines[]`
- `ops[]`

这里的 `ops[]` 可以理解为：

- stage 内声明要挂接的处理节点
- 运行时最终会把 `ops[].name` 映射到对应 `Core`

所以新增能力时通常要确认两件事：

1. `PipelineRuntimeRecipe` 是否能识别这个领域角色
2. 对应 `Stage` 是否能根据 `ops[].name` 正确选择 `Core`

## 什么时候需要新增 Stage

不一定每新增一个 core 都要新增一个 stage。

### 只新增 core 就够的情况

例如：

- 给 `asr` 新增一个模型实现
- 给 `vad` 新增另一个后端实现

这时通常只需要：

- 在对应领域目录下新增一个 core 文件
- 接到已有 `CoreFactory`
- 让已有 `Stage` 通过名称创建它

### 需要新增 stage 的情况

例如：

- 新增一个全新领域
- 新增的能力需要新的 token/segment 生命周期
- 新增的能力需要新的事件语义

这时才建议新增一个新的 `Stage`

## 新增能力时建议的最小检查清单

1. 目录是否放在正确层次  
   - 运行时骨架放 `runtime/`
   - 领域实现放 `domain/`

2. `Stage` 和 `Core` 是否分工清楚  
   - 不要把 runtime 路由塞进 core
   - 不要把算法状态机塞进 stage

3. 是否走注册式创建  
   - 不要在 `Stage` 里继续堆 `if/else` 硬编码

4. 是否补了最小测试  
   - `Core` 初始化测试
   - 领域最小功能测试
   - 如果接入主链，再补 engine/runtime smoke

## 推荐测试层级

### Core 层

优先补：

- 初始化测试
- 最小输入输出测试
- 边界条件测试

### Runtime 层

如果能力接入主运行时，再补：

- `PipelineRuntime.*`
- `TestAsrRealAudio.*`

## 当前推荐开发顺序

新增一个能力时，建议按这个顺序推进：

1. 先写 `Core`
2. 再接 `CoreFactory`
3. 再让已有 `Stage` 调起来
4. 最后再接入 `EngineRuntime` 或 DAG 配置

这样最稳，也最容易定位问题。

## 一句话结论

当前仓库的扩展方式建议统一成：

- `runtime/` 放调度骨架
- `domain/` 放领域实现
- `Stage` 负责运行时边界
- `Core` 负责领域处理
- 新能力优先通过 `CoreFactory` 接入，而不是重新引入旧式包装层
