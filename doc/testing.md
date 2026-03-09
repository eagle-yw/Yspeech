# 测试

## 运行所有测试

```bash
./build/test
```

## 运行特定测试

```bash
# Pipeline 测试
./build/test --gtest_filter=TestPipeline.*

# Operator 测试
./build/test --gtest_filter=TestOp.*

# Aspect 测试
./build/test --gtest_filter=TestAspect.*
```
