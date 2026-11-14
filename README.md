# Smart Shelf Edge-to-Cloud System

[![Engineering CI](https://github.com/KristenYue/smart-supermarket-shelf/actions/workflows/ci.yml/badge.svg)](https://github.com/KristenYue/smart-supermarket-shelf/actions/workflows/ci.yml)
![C++17](https://img.shields.io/badge/firmware-C%2B%2B17-00599C)
![AWS IoT](https://img.shields.io/badge/cloud-AWS%20IoT-FF9900)
![Java 17](https://img.shields.io/badge/backend-Java%2017-ED8B00)
![Python 3.12](https://img.shields.io/badge/verification-Python%203.12-3776AB)

面向智能零售场景的双节点 AIoT 系统：两台 Arduino MKR WiFi 1010 采集环境、顾客互动和库存信号，在边缘完成状态判定与现场告警，通过 ATECC608A 设备身份和 MQTT/TLS 接入 AWS IoT Core，云端完成存储、分析、查询和告警同步。

这个仓库重点展示的不只是“传感器接上云”，而是设备软件在断网、乱序、重复投递、未校时和设备重启条件下如何保持可解释、可验证的数据链路。

## 工程能力一览

| 方向 | 实现证据 |
|---|---|
| 嵌入式 C++ | C++17 纯状态机、固定容量 Outbox、无动态内存、阈值原子更新 |
| 设备与安全 | Arduino MKR WiFi 1010、ATECC608A、BearSSL、独立 Thing/证书、最小权限 Topic |
| IoT 协议 | MQTT QoS 1、命令/配置通道、Telemetry v2、重启会话、幂等与乱序处理 |
| 云端链路 | AWS IoT Core、服务端接收时间、DynamoDB、Java 17/Javalin、Spring Boot/MySQL |
| 研发效能 | CMake/CTest、pytest 契约与故障注入、Maven、50,000 条稳定性场景、GitHub Actions |

## 实际场景

系统把超市货架拆为两个职责不同的边缘节点：

- `smart-shelf-01`：监测温度、湿度、气压和顾客靠近/拿放商品行为；环境持续越界后告警。
- `smart-shelf-02`：融合重量、光线、声音和求助按钮，判断遮挡、碰撞、低库存、无库存和人工求助。

货架 2 对多源异常执行明确优先级：

```text
人工求助 > 遮挡异常 > 声音异常 > 无库存 > 低库存 > 正常
```

状态机在设备本地运行，即使云端暂时不可用，现场 LED/蜂鸣器仍能响应；恢复连接后，固定容量 Outbox 按 FIFO 补发缓存消息。

## 架构

```text
┌──────────────────────── Edge ────────────────────────┐
│ Shelf 1                 Shelf 2                      │
│ ENV + ultrasonic       HX711 + light/sound/button   │
│ sampling + alert       C++17 priority state machine │
│           └──── fixed-capacity MQTT outbox ────┘    │
│                ATECC608A identity + MQTT/TLS         │
└──────────────────────────┬───────────────────────────┘
                           ▼
                  AWS IoT Core / IoT Rule
                           ▼
                       DynamoDB
                           ▼
              Java analysis & control API
                           ▼
          dashboard / Spring alert service / MySQL
```

## 关键设计

### 重启感知的 Telemetry v2

每条遥测使用 `{deviceID}:{bootId}:{sequence}` 作为 `messageId`：

- `sequence` 负责同一次启动内的单调顺序，不依赖网络时钟；
- `bootId` 隔离重启前后两个序列空间；
- `timeSynchronized` 明确区分可信时间与启动早期的 `timestamp=0`；
- 网关识别重复消息、同身份不同载荷和乱序消息。
- AWS IoT Rule 生成 `receivedAt` 作为 DynamoDB 排序键，避免未校时消息覆盖。

详见 [Telemetry v2 协议](docs/TELEMETRY_V2.md)。

### 固定容量离线队列

`FixedOutbox<4, 96, 512>` 在编译期固定容量和帧上限，不依赖动态内存。断网或发送失败时入队，队列满时淘汰最旧消息并保留丢弃计数，重连后按 FIFO 恢复发送。

### 可移植固件核心

硬件访问留在 `.ino` 适配层；状态机、阈值约束和 Outbox 位于 `firmware/core/`，可用标准 C++17 编译器在 Windows/Linux 上直接执行单元测试。

更多代码级说明见 [设计亮点](docs/DESIGN_HIGHLIGHTS.md)。

## 仓库结构

```text
.
├─ firmware/
│  ├─ core/                         # C++17 状态机、Outbox 与 CTest
│  ├─ shelf1_env_engagement/        # 环境与顾客互动节点
│  └─ shelf2_inventory_assistance/  # 库存与求助节点
├─ iot-test-harness/                # Python 契约、故障注入、稳定性测试
├─ cloud-service/                   # Java/Javalin 分析与控制 API
├─ spring-alert-service/            # Spring Boot 告警同步与 MySQL 查询
├─ infra/cloudformation.yaml        # DynamoDB、IoT Rule、IoT Policy
├─ docs/                            # 协议、工程日志、Bad Case、设计说明
└─ .github/workflows/ci.yml         # C++/Python/Java/Spring CI
```

## 自动化验证

CI 对四个层次分别给出回归证据：

| 测试层 | 覆盖内容 |
|---|---|
| C++/CTest | 状态优先级、阈值边界、Outbox FIFO/溢出/超长帧 |
| Python/pytest | 遥测契约、命令与配置白名单、幂等、乱序、重启、重连 |
| Java/Maven | 环境与库存分析、来源追踪、启动会话级顺序 |
| Spring/Maven | 告警同步、设备隔离、API 参数与 JDBC 存储 |

稳定性脚本以固定随机种子发送 50,000 条双设备遥测，并注入重复和乱序，核对接收结果与持久化计数。

```powershell
# Windows：C++ 固件核心
.\scripts\build_firmware_core.cmd

# Python 协议与稳定性
cd iot-test-harness
python -m pip install -e ".[test]"
python -m pytest --cov=smart_shelf_testing --cov-report=term-missing --cov-fail-under=90
python tools/run_stability.py --messages 50000

# Java 云服务
cd ..\cloud-service
mvn --batch-mode verify

# Spring 告警扩展
cd ..\spring-alert-service
mvn --batch-mode test
```

测试策略见 [AIoT 测试计划](iot-test-harness/docs/TEST_PLAN.md)，实施过程见 [工程日志](docs/ENGINEERING_LOG.md)，问题闭环见 [Bad Cases](docs/BAD_CASES.md)。

## MQTT 接口

| Topic | 方向 | 用途 |
|---|---|---|
| `smartshelf/{deviceID}/telemetry` | 设备 → 云端 | 周期遥测 |
| `smartshelf/{deviceID}/events` | 设备 → 云端 | 状态变化与互动事件 |
| `smartshelf/{deviceID}/alerts` | 设备 → 云端 | 告警触发与恢复 |
| `smartshelf/cmd/{deviceID}` | 云端 → 设备 | `ON/OFF/FLASH/AUTO/ACK` |
| `smartshelf/{deviceID}/config` | 云端 → 设备 | 阈值配置 |

配置先写入候选对象，完整验证上下界关系后一次性提交；非法字段、越界数值和破坏阈值关系的请求会被拒绝并产生审计事件。

## 硬件与接线

| 节点 | 传感器/执行器 | 接口 |
|---|---|---|
| Shelf 1 | MKR ENV Shield | Shield / I2C |
| Shelf 1 | Grove 超声波 | D5 |
| Shelf 1 | Grove 光线 | A1 |
| Shelf 1 | 告警 LED | `LED_BUILTIN` |
| Shelf 2 | HX711 I2C 称重模块 | I2C |
| Shelf 2 | 声音 / 光线 | A0 / A1 |
| Shelf 2 | 求助按钮 / LED / 蜂鸣器 | D4 / D3 / D2 |

Arduino 库：`WiFiNINA`、`ArduinoMqttClient`、`ArduinoBearSSL`、`ArduinoECCX08`、`Arduino_MKRENV`、`DFRobot_HX711_I2C`。

## 设备与云端配置

1. 为两台设备分别创建 AWS IoT Thing、Client ID 和证书。
2. 将证书分别绑定到对应 Thing，并将私钥写入各自 ATECC608A 槽位；Client ID 必须与 Thing Name 相同。
3. 将 `secrets.example.h` 复制为 `secrets.h`，填写 Wi-Fi、IoT Endpoint、Client ID 和设备证书。
4. 部署 `infra/cloudformation.yaml` 创建 DynamoDB 表、IoT Rule 与最小权限 Policy。
5. 在 Arduino IDE 中选择 MKR WiFi 1010，分别验证并上传两个固件。

`secrets.h`、`.env`、私钥和证书文件已加入 `.gitignore`。Java 服务通过 AWS Profile、IAM Role 或临时凭据访问云资源，不在源码和前端中保存 Access Key。

Java 运行示例：

```powershell
$env:AWS_REGION = "eu-west-2"
$env:AWS_PROFILE = "your-profile"
$env:AWS_IOT_DATA_ENDPOINT = "https://your-endpoint-ats.iot.eu-west-2.amazonaws.com"
$env:API_TOKEN = "a-long-random-token"
cd cloud-service
mvn clean package
java -jar target/smart-shelf-cloud-service-1.0.0.jar
```

Spring Boot + MySQL 扩展的配置和 API 见 [模块说明](spring-alert-service/README.md)。

## 验证分层

当前仓库把验证证据分为两层：CI 持续执行硬件无关的 C++、协议和服务回归；接入具体开发板、传感器与 AWS 账号后，按测试计划补充串口、弱网、证书和实物执行器记录。这样代码级结论与现场验证证据保持可追溯，不混用测试口径。

## License

[MIT](LICENSE)
