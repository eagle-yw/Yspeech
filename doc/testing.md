# 测试

## 运行所有测试

```bash
./build/test
```

## 先列出测试

仓库中的测试组比较多，先列出名字再筛选通常最稳妥：

```bash
./build/test --gtest_list_tests
```

## 常用筛选

```bash
./build/test --gtest_filter=TestEngine.*
./build/test --gtest_filter=TestSileroVad.*
./build/test --gtest_filter=TestKaldiFbankOp.*
./build/test --gtest_filter=TestKaldiFbankPerformance.*
./build/test --gtest_filter=TestAsrRealAudio.*
./build/test --gtest_filter=AspectTest.*
./build/test --gtest_filter=CapabilityTest.*
```

## 说明

- 测试二进制名就是 `build/test`
- 不同文件里的测试前缀并不完全统一，所以不要假设 `TestOp.*` 这类旧过滤器一定存在
- 涉及真实模型和音频的测试通常对本地模型文件更敏感
