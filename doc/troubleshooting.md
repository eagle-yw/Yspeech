# 故障排查

## 常见问题

### 1. 配置验证失败

**错误**：
```
Config validation failed:
  - pipelines[0]: missing required field 'id'
```

**解决**：检查配置文件中是否包含所有必填字段。

### 2. Operator 创建失败

**错误**：
```
Failed to create operator: Unknown operator type: Xxx
```

**解决**：确保 Operator 已正确注册。

### 3. RingBuffer 阻塞

**现象**：数据输入线程阻塞

**解决**：
- 增大 RingBuffer 容量
- 提高 Pipeline 处理速度
- 检查是否有死锁

## 日志级别

```json
{
  "global": {
    "properties": {
      "log_level": "DEBUG"  // DEBUG, INFO, WARNING, ERROR
    }
  }
}
```
