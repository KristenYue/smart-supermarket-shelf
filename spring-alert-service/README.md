# Spring Boot 告警查询服务

这是智能货架项目的可选后端扩展。现有 Javalin 服务仍从 DynamoDB 读取设备遥测并生成分析结果；本服务通过现有只读 API 同步异常状态到 MySQL，提供按设备查询的告警历史。它不替代设备端、AWS IoT 或原有分析链路。

## 数据流

`Arduino → AWS IoT Core → DynamoDB → Javalin 分析 API → Spring Boot → MySQL`

## 本地运行

1. 使用 MySQL 执行 `schema.sql`，创建 `smart_shelf` 数据库和 `alert_event` 表，并为应用账号授予该表的读写权限。
2. 启动已有 `cloud-service`（默认端口 8080），设置其 `API_TOKEN`。
3. 配置环境变量 `MYSQL_URL`、`MYSQL_USER`、`MYSQL_PASSWORD`、`SHELF_SOURCE_URL` 和 `SHELF_SOURCE_TOKEN`。令 `SHELF_SOURCE_TOKEN` 与原服务的 `API_TOKEN` 相同。不要提交密码或令牌。
4. 使用 Java 17+ 和 Maven 运行 `mvn test`，再运行 `mvn spring-boot:run`。新服务仅绑定 `127.0.0.1:8081`。

示例配置（PowerShell）：

```powershell
$env:MYSQL_URL = 'jdbc:mysql://127.0.0.1:3306/smart_shelf?serverTimezone=UTC'
$env:MYSQL_USER = 'smart_shelf'
$env:MYSQL_PASSWORD = '<local-password>'
$env:SHELF_SOURCE_URL = 'http://127.0.0.1:8080'
$env:SHELF_SOURCE_TOKEN = '<same-as-API_TOKEN>'
mvn test
mvn spring-boot:run
```

## API

- `GET /api/devices/smart-shelf-01/telemetry?limit=20`：代理原服务的遥测查询。
- `GET /api/devices/smart-shelf-01/alerts?limit=20`：从 MySQL 查询最新告警。
- 同样支持 `smart-shelf-02`；`limit` 取值 1–100。

同步任务每 10 秒读取各设备最近 100 条分析结果，只持久化非 `NORMAL` 状态。MySQL 唯一键 `(device_id, source_timestamp, state)` 使重复轮询不产生重复告警；若原服务长时间不可用，超过最近 100 条窗口的旧记录可能无法补录，因此这里不声称完整的历史同步或生产级交付保证。

## 验证范围

单元测试覆盖正常状态过滤、无效时间戳跳过、设备隔离、参数范围校验与上游不可用响应。MySQL 去重行为需要在真实 MySQL 实例上验证：连续同步同一批分析结果两次，再查询 `alert_event`，同一设备、时间戳和状态应只有一行。
