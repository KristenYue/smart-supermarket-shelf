# Smart Shelf IoT Test Harness

面向智能货架设备—MQTT—云端链路的Python自动化测试模块，覆盖遥测契约、命令与配置安全、重复/乱序消息、断线重连和高消息量稳定性验证。

## 快速开始

```powershell
python -m pip install -e ".[test]"
python -m pytest
python tools/run_stability.py --messages 50000
```

测试计划见[`docs/TEST_PLAN.md`](docs/TEST_PLAN.md)，本地实测结果见[`docs/TEST_REPORT.md`](docs/TEST_REPORT.md)。该模块采用硬件无关的参考网关，适合本地和CI回归；真实Arduino接线、传感器精度、Wi-Fi弱网、TLS证书和AWS IoT在线链路仍需使用实物及授权环境验证。
