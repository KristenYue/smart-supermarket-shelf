# Bad Cases 与闭环记录

## BC-001：离线时主循环提前返回

- 现象：Wi-Fi 或 MQTT 不可用时，固件在连接逻辑后提前返回，传感器采样与遥测生成一并停止。
- 风险：断网期间缺少现场状态演进，网络恢复后也没有可补发数据。
- 根因：通信可用性与设备业务循环被绑定。
- 修复：连接尝试改为定时执行；采样和状态机始终运行；发送失败进入固定容量 Outbox，重连后 FIFO 刷新。
- 回归证据：C++ Outbox 测试覆盖 FIFO、满队列丢弃和超长载荷拒绝；Python 端到端测试覆盖重连后序列连续。

## BC-002：使用时间戳作为唯一顺序依据

- 现象：设备启动早期 `WiFi.getTime()` 可能返回 0，网络校时也可能产生跳变。
- 风险：不同遥测拥有相同时间戳，或新消息时间小于旧消息，导致误判重复/乱序。
- 根因：把业务顺序等同于墙上时钟。
- 修复：Telemetry v2 增加单调 `sequence` 与 `timeSynchronized`，时间未同步时显式发送 `timestamp=0`。
- 回归证据：`test_sequence_ordering_survives_unsynchronized_clock`。

## BC-003：设备重启后序列号归零

- 现象：仅使用 `(deviceID, sequence)` 时，设备重启后的 `sequence=1` 会小于云端已记录序号。
- 风险：重启后的合法新数据被当作旧数据丢弃。
- 根因：消息身份缺少启动会话维度。
- 修复：增加 `bootId`，消息身份升级为 `{deviceID}:{bootId}:{sequence}`，云端按启动会话维护顺序窗口。
- 回归证据：`test_new_boot_session_may_restart_sequence` 与 Java `sequenceOrderingIsScopedToBootSession`。

## BC-004：多个传感器同时异常时状态取决于代码顺序

- 现象：按钮、光线、声音、重量可能在同一周期同时触发。
- 风险：现场执行器结果不稳定，维护人员无法从需求直接推导状态。
- 根因：优先级隐含在 Arduino 主循环的条件分支中。
- 修复：抽离纯 C++17 状态机并把优先级固化为可测试规则。
- 回归证据：`firmware/core/tests/test_core.cpp` 覆盖组合异常与阈值边界。

## BC-005：远程阈值逐字段写入可能留下半更新配置

- 现象：若先写入 `lowStockG`，再发现 `outOfStockG >= lowStockG`，设备可能保留部分新值。
- 风险：配置请求虽被拒绝，运行参数却已经改变。
- 根因：缺少候选配置与整体校验阶段。
- 修复：先复制候选阈值，完整解析和验证后一次性提交；失败时发布 `config_rejected` 事件。
- 回归证据：共享阈值校验单元测试和 Python 配置契约测试。

## BC-006：MSVC 安全警告暴露不必要的 C 字符串复制

- 现象：`strcpy` 在 MSVC `/W4` 下触发 C4996。
- 风险：虽然调用前已有长度检查，但实现意图不够直接，跨编译器结果不够干净。
- 根因：采用终止符驱动的复制接口。
- 修复：预先计算长度，使用 `memcpy` 复制精确字节并显式写入 `\0`。
- 回归证据：Windows clean build 与 CTest。

## BC-007：本机工具不在默认 PATH

- 现象：默认 Python 缺少 pytest；`mvn` 不在 PATH；直接调用 CMake 时未加载 MSVC 开发环境。
- 影响：首次验证命令失败，但不属于项目代码缺陷。
- 处理：Python 使用隔离依赖环境；Maven 使用 IntelliJ 随附版本；Windows C++ 通过 `VsDevCmd.bat` 初始化工具链。CI 使用固定的 Ubuntu、Python 3.12、Java 17 和 CMake 环境消除本机差异。

## BC-008：测试替身依赖 JVM 动态 Attach

- 现象：Spring 测试首次执行停在 Mockito inline mock maker 初始化，本机 JVM 无法打开 Attach 命名管道。
- 风险：业务断言尚未开始，测试进程即因运行环境能力差异挂起。
- 根因：简单接口测试使用了运行时字节码代理，验证成本高于实际需要。
- 修复：将 `ShelfSource` 和 `AlertStore` 的 Mockito mock 替换为小型内存 Fake，测试数据与调用记录显式可见。
- 回归证据：Spring 模块 5 项测试在 4.968 秒内全部通过，不再加载动态 Java Agent。

## BC-009：历史构建产物带只读属性

- 现象：Maven 复制 `application.properties` 时收到 `Access denied`。
- 影响：源代码编译前的资源阶段失败，与业务逻辑无关。
- 根因：既有 `target/classes/application.properties` 带 Windows `ReadOnly` 属性。
- 处理：清除生成目录中构建产物的只读属性后重跑；`target/` 已被 Git 忽略，CI 从干净工作区生成，不继承该状态。

## BC-010：未校时遥测在 DynamoDB 中发生主键覆盖

- 现象：原遥测表主键为 `(deviceID, timestamp)`；设备启动早期会连续发送 `timestamp=0`。
- 风险：同一设备多条合法遥测写入相同主键，后写数据覆盖先写数据，Outbox 虽补发成功但历史仍丢失。
- 根因：设备时间同时承担业务时间和云端存储唯一键两种职责。
- 修复：IoT Rule 使用 AWS IoT SQL `timestamp()` 增加服务端 `receivedAt`，v2 遥测表改用 `(deviceID, receivedAt)`；Java 在设备时间无效时使用接收时间生成分析时间。
- 迁移：新建 `SmartShelfTelemetryV2`，切换规则和服务配置，避免对已有命名表执行不可原地完成的主键替换。
