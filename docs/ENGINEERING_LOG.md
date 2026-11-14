# 工程日志

## 2026-11-14：设备端工程化与链路可靠性升级

### 目标

将原有可运行的智能货架原型强化为可解释、可测试、可回归的 AIoT 工程项目，重点补齐嵌入式 C++、状态机、MQTT/TLS、离线恢复和云端数据一致性证据。

### 完成项

1. 建立 C++17/CMake 固件核心，抽离货架 2 状态机、阈值模型和状态字符串映射。
2. 实现无动态内存的固定容量 MQTT Outbox，并接入两套 Arduino 固件。
3. 解耦通信连接与设备业务循环，使断网期间仍进行采样、状态判定和待发送消息缓存。
4. 远程阈值采用“候选配置 → 整体校验 → 原子提交”，拒绝半更新。
5. 发布 Telemetry v2：`schemaVersion`、`messageId`、`bootId`、`sequence`、`timestamp`、`timeSynchronized`。
6. IoT Rule 注入服务端 `receivedAt`，v2 遥测表以接收时间排序，消除未校时消息的主键覆盖。
7. IoT Policy 改用 Thing policy variable，把证书绑定、Client ID 和发布/订阅 Topic 限制到同一设备身份。
8. Python 参考网关改为启动会话内排序和幂等，增加设备重启、未同步时钟、载荷冲突测试。
9. Java 分析服务保存来源消息身份，并把处理游标按设备启动会话隔离。
10. CI 增加 C++ 固件核心任务，并统一 Python、Java、Spring Boot 自动化检查。
11. 整理协议、设计亮点、Bad Case 和验证证据，形成可供代码审查与面试追问的工程记录。

### 关键取舍

- Outbox 采用固定容量而非动态容器：在资源受限设备上优先保证内存上界；满队列时保留最新现场状态，淘汰最旧帧并记录计数。
- 顺序使用 `sequence` 而非时间戳：顺序与时钟可信度解耦；通过 `bootId` 解决设备重启后的序列复位。
- 状态机保持纯函数：硬件读写留在 Arduino 适配层，核心规则可在桌面与 CI 中快速回归。
- v2 契约对新遥测严格校验；Java 分析侧保留 legacy 回退，便于已有 DynamoDB 数据平滑读取。

### 验证命令

```powershell
.\scripts\build_firmware_core.cmd

cd iot-test-harness
python -m pip install -e ".[test]"
python -m pytest --cov=smart_shelf_testing --cov-report=term-missing --cov-fail-under=90
python tools/run_stability.py --messages 50000

cd ..\cloud-service
mvn --batch-mode verify

cd ..\spring-alert-service
mvn --batch-mode test
```

具体故障复现、根因和回归点见 [Bad Cases](BAD_CASES.md)。
