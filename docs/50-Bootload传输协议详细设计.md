# Bootload 传输协议详细设计

## 1. 协议约束

- 物理链路：以太网
- 传输协议：Modbus TCP
- 功能码限制：升级控制与数据传输只使用保持寄存器
- 推荐功能码：
  - `0x03` Read Holding Registers
  - `0x06` Write Single Holding Register
  - `0x10` Write Multiple Holding Registers
- 默认端口：`502`
- 默认 Unit ID：`1`

## 2. 地址约定

本文中的寄存器地址使用 Modbus 协议地址，也就是从 `0` 开始的地址。

若使用 40001 风格显示地址，换算方式为：

```text
display_address = 40001 + protocol_address
```

例如协议地址 `0x0100` 等于十进制 `256`，显示地址为 `40257`。

## 3. 保持寄存器表

| 协议地址 | 显示地址 | 名称 | 访问 | 说明 |
|---:|---:|---|---|---|
| `0x0100` | `40257` | `BOOT_COMMAND` | W | BOOT 命令寄存器 |
| `0x0101` | `40258` | `BOOT_STATUS` | R | Bootloader 状态 |
| `0x0102` | `40259` | `BOOT_ERROR` | R | 最近一次错误码 |
| `0x0103` | `40260` | `SESSION_ID` | R/W | 升级会话 ID |
| `0x0104` | `40261` | `PACKET_INDEX_H` | W | 包序号高 16 位 |
| `0x0105` | `40262` | `PACKET_INDEX_L` | W | 包序号低 16 位 |
| `0x0106` | `40263` | `PACKET_LENGTH` | W | 本包有效字节数 |
| `0x0107` | `40264` | `PACKET_CRC_H` | W | 本包 CRC32 高 16 位 |
| `0x0108` | `40265` | `PACKET_CRC_L` | W | 本包 CRC32 低 16 位 |
| `0x0109` | `40266` | `IMAGE_SIZE_H` | W | 固件总长度高 16 位 |
| `0x010A` | `40267` | `IMAGE_SIZE_L` | W | 固件总长度低 16 位 |
| `0x010B` | `40268` | `IMAGE_CRC_H` | W | 固件 CRC32 高 16 位 |
| `0x010C` | `40269` | `IMAGE_CRC_L` | W | 固件 CRC32 低 16 位 |
| `0x0200` | `40513` | `DATA_BUFFER_START` | W | 固件数据缓冲区起始地址 |

数据缓冲区范围建议为 `0x0200 ~ 0x0277`，共 `120` 个保持寄存器，可承载 `240` 字节数据。
选择 `120` 个寄存器是为了满足 Modbus `0x10` 单帧最多写 `123` 个保持寄存器的限制，并保留一定协议余量。

## 4. 命令字

| 命令值 | 名称 | 说明 |
|---:|---|---|
| `0xB007` | `ENTER_BOOTLOADER` | App 收到后进入 Bootloader |
| `0xB101` | `SESSION_START` | Bootloader 建立升级会话 |
| `0xB102` | `ERASE_APP` | 擦除 App 区 |
| `0xB103` | `TRANSFER_PACKET` | 提交当前数据包 |
| `0xB104` | `VERIFY_IMAGE` | 校验完整固件 |
| `0xB105` | `ACTIVATE_IMAGE` | 设置启动标志并重启 |
| `0xB1FF` | `ABORT` | 中止升级 |

当前上位机已将 APP 复位流程和 BOOT 升级流程拆分。APP 流程只向保持寄存器写入 `0xB007`，由 APP 写入 Flash boot_flag 并复位；BOOT 流程从 Bootloader 已启动后的会话流程开始。

## 5. 状态字

| 状态值 | 名称 | 说明 |
|---:|---|---|
| `0x0000` | `IDLE` | 空闲 |
| `0x0001` | `BUSY` | 正在执行命令 |
| `0x0002` | `READY` | 可接收下一步 |
| `0x0003` | `PACKET_OK` | 当前包写入成功 |
| `0x0004` | `VERIFY_OK` | 整包校验成功 |
| `0x8000` | `ERROR` | 发生错误，读取 `BOOT_ERROR` |

## 6. 错误码

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `0x0001` | `INVALID_COMMAND` | 命令值非法 |
| `0x0002` | `INVALID_SESSION` | 会话 ID 无效 |
| `0x0003` | `PACKET_INDEX_ERROR` | 包序号不连续 |
| `0x0004` | `PACKET_LENGTH_ERROR` | 包长度非法 |
| `0x0005` | `PACKET_CRC_ERROR` | 当前包 CRC 错误 |
| `0x0006` | `FLASH_ERASE_ERROR` | Flash 擦除失败 |
| `0x0007` | `FLASH_WRITE_ERROR` | Flash 写入失败 |
| `0x0008` | `IMAGE_CRC_ERROR` | 固件总 CRC 错误 |
| `0x0009` | `VERSION_REJECTED` | 版本不允许升级或回退 |

## 7. 升级流程

上位机界面需要分别填写 `APP IP/Port` 和 `BOOT IP/Port`。两个地址可以不同，APP 阶段和 BOOT 阶段互不自动串联。

### 7.1 APP 进入 Bootloader 流程

1. 上位机连接 `APP IP/Port`。
2. 上位机向 APP 的 `BOOT_COMMAND` 写入 `ENTER_BOOTLOADER(0xB007)`。
3. APP 收到命令后，只负责写入 Flash boot_flag 升级模式并延时复位。
4. 上位机不在该流程中等待 Bootloader 重连。

### 7.2 BOOT 固件升级流程

1. Bootloader 启动并在 `BOOT IP/Port` 提供 Modbus TCP 服务。
2. 上位机连接 `BOOT IP/Port`，读取 `BOOT_STATUS`，确认状态为 `READY`。
3. 上位机写入 `SESSION_ID`、`IMAGE_SIZE`、`IMAGE_CRC`。
4. 上位机向 `BOOT_COMMAND` 写入 `SESSION_START`。
5. 上位机轮询 `BOOT_STATUS`，等待 `READY`。
6. 上位机向 `BOOT_COMMAND` 写入 `ERASE_APP`。
7. 上位机轮询 `BOOT_STATUS`，等待 `READY`。
8. 上位机按 `240` 字节切包，逐包写入数据区 `0x0200 ~ 0x0277`。
9. 每包写入前设置 `PACKET_INDEX`、`PACKET_LENGTH`、`PACKET_CRC`。
10. 每包写入后向 `BOOT_COMMAND` 写入 `TRANSFER_PACKET`。
11. 上位机读取 `BOOT_STATUS`，收到 `PACKET_OK` 后发送下一包。
12. 全部发送后，向 `BOOT_COMMAND` 写入 `VERIFY_IMAGE`。
13. 校验通过后，向 `BOOT_COMMAND` 写入 `ACTIVATE_IMAGE`。
14. Bootloader 写入工作模式 boot_flag `0x000000AA` 并重启进入新 APP。

## 8. 数据打包规则

- 每个保持寄存器为 `16 bit`。
- 固件数据按大端方式放入寄存器：

```text
register_value = (data[2*n] << 8) | data[2*n + 1]
```

- 若最后一个包为奇数字节，低 8 位补 `0x00`，但 `PACKET_LENGTH` 必须填写真实有效字节数。
- 包 CRC 使用当前包原始有效字节计算，不包含补齐字节。
- 固件 CRC 使用整个固件原始字节计算。

## 9. 超时与重试

- TCP 连接超时：`3s`
- 非固件分包阶段的控制命令/状态轮询间隔：不小于 `500ms`
- 固件分包发送阶段：`100ms/包`
- 单命令最大等待：`3s`
- 擦除阶段最大等待：按 Flash 大小配置，建议 `10s ~ 30s`
- 单包失败重试：`3` 次
- 连续失败后上位机发送 `ABORT`

## 10. 上位机当前实现状态

- 已实现 APP IP/Port 和 BOOT IP/Port 两套输入。
- 已实现 APP TCP 和 BOOT TCP 独立连接状态指示灯。
- 已实现 APP 流程按钮：只写入 `ENTER_BOOTLOADER(0xB007)`，由 APP 写 Flash flag 并复位。
- 已实现 BOOT 流程按钮：只执行 Bootloader 原始升级流程，不再自动发送 APP 复位命令。
- 已实现 `SESSION_START`、`ERASE_APP`、分包写入 `DATA_BUFFER`、`VERIFY_IMAGE`、`ACTIVATE_IMAGE`。
- 已实现升级步骤表和消息框调试输出。
- 已实现节奏控制：固件分包阶段 `100ms/包`，其余流程指令间隔不小于 `500ms`。
