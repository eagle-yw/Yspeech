# 性能优化

## 单级 vs 多级性能

| 模式 | 吞吐 | 延迟 | 资源 |
|------|------|------|------|
| **单级** | 中 | 低 | 少 |
| **两级** | 高（+70%） | 中 | 多 |

## 优化建议

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

## 性能基准

```bash
# 运行性能测试
./build/test --gtest_filter=PerfTest.*
```
