# 上位机软件架构（Python + PyQt）

## 1. 技术选型

- Python：`3.11+`
- GUI：`PyQt6`
- Modbus TCP：`pymodbus`
- 日志：`loguru`（或标准 logging）
- 打包：`pyinstaller`（后续发布阶段）

## 2. 工程分层

- `ui/`：窗口与控件，不直接写协议细节。
- `service/`：升级状态机、任务编排、异常处理。
- `protocol/`：Modbus 客户端与 Bootload 指令封装。
- `model/`：DTO 与状态枚举。
- `util/`：固件文件解析、CRC、通用工具。

## 3. UI 页面建议

1. 连接区：
   - 设备 IP、端口、连接按钮、在线状态灯
2. 固件区：
   - 固件选择、文件信息（版本、大小、CRC）
3. 升级区：
   - 开始/停止、当前阶段、进度条、速度、剩余时间
4. 日志区：
   - 实时日志、错误过滤、导出日志

## 4. 线程模型建议

- 主线程：仅负责 UI 渲染与用户交互。
- 工作线程：执行网络通信与升级流程。
- UI 与工作线程通过信号槽通信，避免界面卡死。

## 5. 状态机（上位机）

- `IDLE`
- `CONNECTED`
- `PRECHECK_OK`
- `IN_BOOTLOADER`
- `TRANSFERRING`
- `VERIFYING`
- `DONE`
- `FAILED`

状态转换必须由服务层统一管理，不允许 UI 层直接跳状态。

## 6. 日志与追溯

- 记录维度：
  - 时间戳
  - 设备标识（IP/MAC/SN）
  - 升级会话 ID
  - 当前阶段
  - 错误码与原始响应
- 日志文件建议按天轮转：
  - `logs/upgrade_YYYYMMDD.log`

## 7. 测试建议

- 单元测试：
  - 固件切包
  - CRC 校验
  - 状态机转换
- 集成测试：
  - 模拟设备响应（Mock Modbus Server）
  - 丢包、超时、NACK 场景

