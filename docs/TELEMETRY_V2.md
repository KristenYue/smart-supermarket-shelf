# Telemetry v2 协议

Telemetry v2 为离线缓存、乱序到达、重复投递和设备重启建立可验证的消息身份。

## 消息身份

每条遥测包含：

| 字段 | 含义 |
|---|---|
| `schemaVersion` | 当前固定为 `2.0` |
| `deviceID` | AWS IoT Thing 对应的设备标识 |
| `bootId` | 本次设备启动会话的正整数标识 |
| `sequence` | 当前启动会话内单调递增序号 |
| `messageId` | `{deviceID}:{bootId}:{sequence}` |
| `timestamp` | 已同步时为 Unix 秒；未同步时为 `0` |
| `timeSynchronized` | 设备时钟是否已取得可信网络时间 |

AWS IoT Rule 在接收消息时增加 `receivedAt=timestamp()`（毫秒），DynamoDB 遥测表以 `(deviceID, receivedAt)` 为主键。设备时间用于现场语义，服务端接收时间用于持久化顺序和唯一性，因此连续多条 `timestamp=0` 不会相互覆盖。

`sequence` 不依赖墙上时钟，因此设备刚启动、NTP 尚未同步或网络时间回拨时，网关仍可判断同一启动会话内的先后关系。`bootId` 将重启后的新序列与上一次运行隔离，使序列从 1 重新开始仍可被接受。

## 网关处理规则

1. 先验证设备白名单、字段类型、传感器范围和消息身份绑定关系。
2. 完全相同的 `(deviceID, bootId, sequence, payload)` 返回 `duplicate`，不重复写入。
3. 相同身份但载荷不同返回 `payload_conflict`。
4. 同一启动会话内小于已接收最大序号的消息返回 `out_of_order`。
5. 新 `bootId` 建立独立顺序窗口，允许序号从 1 开始。

## 示例

```json
{
  "deviceID": "smart-shelf-02",
  "schemaVersion": "2.0",
  "messageId": "smart-shelf-02:18452177:42",
  "bootId": 18452177,
  "sequence": 42,
  "timestamp": 1800000000,
  "timeSynchronized": true,
  "weightG": 18.5,
  "soundRaw": 32,
  "lightRaw": 486,
  "buttonPressed": false,
  "state": "LOW_STOCK",
  "uptimeMs": 210000,
  "scaleHealthy": true
}
```

## 兼容策略

Java 分析服务对旧数据保留兼容：缺少 `sequence` 时使用 `timestamp` 排序，缺少 `bootId` 时归入 legacy 会话。新固件和 Python 契约测试只生成和接受 v2 遥测，以避免新链路静默降级。

遥测表从 `SmartShelfTelemetry` 升级为 `SmartShelfTelemetryV2`，用于承载新的 `receivedAt` 排序键。已有环境应新建 v2 表、更新 `TELEMETRY_TABLE` 后切换 IoT Rule，确认新链路稳定后再按保留策略处理旧表。
