# 智能超市货架系统

这是智能超市货架项目的整理和安全优化版本。系统由两套 Arduino MKR WiFi 1010 货架节点、AWS IoT Core、DynamoDB、Java 云端服务和网页仪表盘组成。

## 项目功能

### 货架 1：环境与顾客互动监测

设备编号：`smart-shelf-01`

主要功能：

- 使用 MKR ENV Shield 测量温度、湿度和气压
- 使用超声波传感器判断顾客是否靠近货架
- 使用光线传感器检测商品拿起、放回等互动行为
- 统计访客数量、互动次数和累计停留时间
- 环境持续异常 30 秒后产生报警
- 通过 AWS IoT Core 接收远程 LED 控制和阈值设置

### 货架 2：库存与顾客求助监测

设备编号：`smart-shelf-02`

主要功能：

- 使用称重传感器和 HX711 检测库存重量
- 使用光线传感器检测货架前方遮挡
- 使用声音传感器检测碰撞或异常扰动
- 使用实体按钮发送顾客求助请求
- 使用 LED 和蜂鸣器提供现场报警
- 通过 AWS IoT Core 接收远程控制命令

## 系统架构

```text
货架 1 ─┐
        ├─ AWS IoT Core ─ IoT Rule ─ DynamoDB
货架 2 ─┘                              │
                                      ▼
                              Java 分析与 API 服务
                                      │
                                      ▼
                                  网页仪表盘
```

- 两块 Arduino 使用 TLS 加密连接 AWS IoT Core。
- 设备私钥保存在 MKR WiFi 1010 的 ATECC608A 安全芯片中。
- AWS IoT Rule 将设备遥测数据写入 DynamoDB。
- Java 服务读取遥测数据、生成分析结果并提供网页 API。
- 网页通过 Java API 获取数据，不直接保存 AWS Access Key。

## 项目目录

```text
smart-supermarket-shelf-optimized/
├─ firmware/
│  ├─ shelf1_env_engagement/
│  │  ├─ shelf1_env_engagement.ino
│  │  └─ secrets.example.h
│  ├─ shelf2_inventory_assistance/
│  │  ├─ shelf2_inventory_assistance.ino
│  │  └─ secrets.example.h
│  └─ tests/
│     └─ weight_test/
│        └─ weight_test.ino
├─ cloud-service/
│  ├─ pom.xml
│  └─ src/main/
│     ├─ java/com/smartshelf/
│     └─ resources/public/index.html
├─ infra/
│  └─ cloudformation.yaml
├─ docs/
├─ .gitignore
└─ README.md
```

## MQTT 主题

| MQTT 主题 | 方向 | 用途 |
|---|---|---|
| `smartshelf/{deviceID}/telemetry` | 设备 → 云端 | 每 5 秒发送一次传感器数据 |
| `smartshelf/{deviceID}/events` | 设备 → 云端 | 状态变化和顾客互动事件 |
| `smartshelf/{deviceID}/alerts` | 设备 → 云端 | 报警触发与恢复事件 |
| `smartshelf/cmd/{deviceID}` | 云端 → 设备 | LED、蜂鸣器和确认命令 |
| `smartshelf/{deviceID}/config` | 云端 → 设备 | 远程修改报警阈值 |

设备支持的控制命令：

- `ON`：打开 LED
- `OFF`：关闭 LED
- `FLASH`：闪烁 LED
- `AUTO`：恢复设备自动控制
- `ACK`：确认货架 2 的求助报警

## Arduino 接线

### 货架 1

| 硬件 | 引脚或接口 |
|---|---|
| MKR ENV Shield | I2C / Shield 接口 |
| Grove 超声波传感器 | D5 |
| Grove 光线传感器 | A1 |
| 报警 LED | `LED_BUILTIN` |

### 货架 2

| 硬件 | 引脚或接口 |
|---|---|
| HX711 I2C 称重模块 | I2C |
| 声音传感器 | A0 |
| 光线传感器 | A1 |
| 顾客求助按钮 | D4 |
| LED | D3 |
| 蜂鸣器 | D2 |

按钮使用 `INPUT_PULLUP` 模式，因此按下时读取为 `LOW`。

## 安装 Arduino 库

打开 Arduino IDE，进入：

```text
工具 → 管理库
```

安装以下库：

- `WiFiNINA`
- `ArduinoMqttClient`
- `ArduinoBearSSL`
- `ArduinoECCX08`
- `Arduino_MKRENV`，仅货架 1 使用
- `DFRobot_HX711_I2C`，仅货架 2 使用

还需要在 Arduino IDE 中安装 Arduino MKR WiFi 1010 开发板支持包，并使用 Firmware Updater 更新 NINA Wi-Fi 固件。

## 配置设备密码和证书

每个货架固件目录中都有一个：

```text
secrets.example.h
```

将它复制为：

```text
secrets.h
```

然后填写：

- Wi-Fi 名称
- Wi-Fi 密码
- AWS IoT Endpoint
- AWS IoT Client ID
- 对应设备的证书

示例：

```cpp
#define SECRET_SSID "你的WiFi名称"
#define SECRET_PASS "你的WiFi密码"

#define AWS_CLIENT_ID "smart-shelf-01"
#define AWS_IOT_ENDPOINT "你的Endpoint-ats.iot.eu-west-2.amazonaws.com"
#define AWS_IOT_PORT 8883
```

两个物理设备必须使用不同的 AWS IoT Thing、Client ID 和设备证书。

`secrets.h` 已被 `.gitignore` 排除，禁止将它上传到 GitHub。

## 默认报警阈值

### 货架 1

| 项目 | 默认值 |
|---|---:|
| 最低温度 | 2 °C |
| 最高温度 | 8 °C |
| 最低湿度 | 30% RH |
| 最高湿度 | 70% RH |
| 顾客检测距离 | 80 cm |
| 光线变化阈值 | 60 |
| 环境报警延迟 | 30 秒 |

### 货架 2

| 项目 | 默认值 |
|---|---:|
| 遮挡光线阈值 | 小于 100 |
| 声音报警阈值 | 大于 150 |
| 低库存阈值 | 不超过 25 g |
| 无库存阈值 | 不超过 1 g |

货架 2 的状态优先级为：

```text
人工求助 → 遮挡报警 → 声音报警 → 无库存 → 低库存 → 正常
```

光线和声音并不是只读取一次。程序会在每个 5 秒上传周期内记录最低光线值和最高声音值，从而提高短时间异常事件的检测能力。

## 创建 AWS 云端资源

`infra/cloudformation.yaml` 可以创建：

- `SmartShelfTelemetry` 遥测数据表
- `SmartShelfAnalysis` 分析结果表
- AWS IoT 数据写入规则
- 设备使用的最小权限 IoT Policy

可以在 AWS CloudFormation 控制台中上传该文件并创建 Stack，建议区域选择：

```text
eu-west-2
```

CloudFormation 不会自动为物理设备创建和配置证书。每块 Arduino 的 Thing、证书和 ATECC608A 安全芯片仍需分别配置。

## 编译 Arduino 固件

### 货架 1

在 Arduino IDE 中打开：

```text
firmware/shelf1_env_engagement/shelf1_env_engagement.ino
```

选择：

```text
开发板：Arduino MKR WiFi 1010
端口：Arduino 当前连接的串口
```

先点击“验证”，确认编译成功，再点击“上传”。

### 货架 2

打开：

```text
firmware/shelf2_inventory_assistance/shelf2_inventory_assistance.ino
```

同样先验证，再上传到第二块 MKR WiFi 1010。

## 编译并运行 Java 服务

运行要求：

- Java 17 或更高版本
- Maven 3.9 或更高版本
- 已配置 AWS Profile、IAM Role 或其他临时 AWS 凭据

禁止把 AWS Access Key 写入 Java 源码或网页文件。

在 PowerShell 中进入云端服务目录：

```powershell
cd cloud-service
```

编译：

```powershell
mvn clean package
```

设置运行参数：

```powershell
$env:AWS_REGION = "eu-west-2"
$env:AWS_PROFILE = "你的AWS配置名称"
$env:AWS_IOT_DATA_ENDPOINT = "https://你的Endpoint-ats.iot.eu-west-2.amazonaws.com"
$env:API_TOKEN = "设置一个较长的随机字符串"
```

启动服务：

```powershell
java -jar target/smart-shelf-cloud-service-1.0.0.jar
```

然后在浏览器中打开：

```text
http://127.0.0.1:8080
```

在网页顶部输入与 `API_TOKEN` 相同的内容，再点击“Apply token”。Token 只保存在当前浏览器标签页的 `sessionStorage` 中。

## Java 服务所需 AWS 权限

运行 Java 服务的 IAM 身份只需要：

- 对两个 DynamoDB 表执行 `dynamodb:Query`
- 对 `SmartShelfAnalysis` 执行 `dynamodb:PutItem`
- 对 `smartshelf/cmd/*` 执行 `iot:Publish`

请遵循最小权限原则，不要授予不必要的管理员权限。

## 安全说明

- 不要上传 `secrets.h`
- 不要上传 `SECRET_SSID.txt`
- 不要在 HTML 中写入 AWS Access Key 或 Secret Key
- 不要上传 `.env`、私钥或真实密码
- 原 HTML 中出现过的 AWS Access Key 应立即在 IAM 中停用并删除
- 上传前运行 `git status`，检查即将提交的文件
- 仓库公开前，确认报告中的团队姓名和邮箱可以公开

原来的 `Back.ino` 没有继续使用。它使用公共匿名 MQTT Broker，并且包含写死的 Wi-Fi 信息。优化后的两个货架固件已经直接集成了经过 AWS IoT TLS 验证的远程控制功能。

## 上传到 GitHub

第一次上传：

```powershell
git add .
git status
git commit -m "上传智能货架项目优化版"
git branch -M main
git remote add origin https://github.com/你的用户名/smart-supermarket-shelf.git
git push -u origin main
```

以后更新代码：

```powershell
git add .
git status
git commit -m "说明本次修改内容"
git push
```

