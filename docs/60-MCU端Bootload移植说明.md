# MCU 端 Bootload 移植说明

## 1. 本次 MCU 端新增文件

- `mcu/bootload_mcu.h`
- `mcu/bootload_mcu.c`
- `mcu/bootload_port_stm32f407.c`

`bootload_mcu.*` 是协议状态机核心，和具体芯片 Flash 无关。

`bootload_port_stm32f407.c` 是 STM32F407VGT6 的 Flash 擦写、校验和 App 跳转参考实现。

## 2. 保留的原有接口

以下已有接口名称未修改：

- `initTcpServer`
- `tcpServerTask`
- `Modbus_App_Init`
- `syncInputReg`
- `syncHoldReg`
- `Modbus_TCP_Process`

`Modbus_TCP_Process` 内部已补齐：

- `0x03` Read Holding Registers
- `0x04` Read Input Registers
- `0x06` Write Single Holding Register
- `0x10` Write Multiple Holding Registers

升级命令只走保持寄存器。

## 3. TCP 端口

当前 MCU TCP 服务端端口已调整为标准 Modbus TCP 端口：

```c
#define TCP_PORT 502
```

上位机默认端口也是 `502`。

## 4. Boot 命令寄存器

App 正常运行时，上位机连接设备 Modbus TCP 后写：

```text
Holding Register: 0x0100
Value:            0xB007
```

MCU 收到后执行：

1. `Bootload_Platform_SetBootRequest`
2. 设置状态 `BOOT_STATUS_BUSY`
3. `Bootload_Platform_Reset`

复位后 Bootloader 检测到 Boot 请求标志，停留在 Bootloader 内等待固件传输。

## 5. Bootloader 运行逻辑

Bootloader 初始化流程建议：

```c
initTcpServer();

while (1)
{
    tcpServerTask();
}
```

`tcpServerTask` 内部会调用：

```c
Bootload_Task();
```

若无 Boot 请求且 App 有效，Bootloader 会跳转 App。

若存在 Boot 请求，Bootloader 保持 Modbus TCP 服务，等待上位机传输固件。

## 6. Flash 地址规划

参考实现采用：

```text
Bootloader: 0x08000000 ~ 0x0801FFFF
App:        0x08020000 ~ 0x080FFFFF
```

也就是 App 起始地址：

```c
#define BOOT_APP_START_ADDR 0x08020000UL
```

STM32F407VGT6 Flash 总容量按 `1 MB` 规划。

若你的 Bootloader 大小不是 `128 KB`，需要同步修改：

- Bootloader 链接脚本
- App 链接脚本
- `BOOT_APP_START_ADDR`
- `BOOT_APP_END_ADDR`

## 7. 固件传输流程

1. 写 `SESSION_ID`
2. 写 `IMAGE_SIZE_H/L`
3. 写 `IMAGE_CRC_H/L`
4. 写 `BOOT_COMMAND = 0xB101`
5. 写 `BOOT_COMMAND = 0xB102` 擦除 App 区
6. 逐包写 `DATA_BUFFER_START(0x0200)` 开始的 120 个保持寄存器
7. 每包写入：
   - `PACKET_INDEX_H/L`
   - `PACKET_LENGTH`
   - `PACKET_CRC_H/L`
   - `DATA_BUFFER`
8. 写 `BOOT_COMMAND = 0xB103`
9. 轮询 `BOOT_STATUS`
10. 全部完成后写 `BOOT_COMMAND = 0xB104`
11. 校验通过后写 `BOOT_COMMAND = 0xB105`

## 8. 当前状态机命令

| 命令 | 值 | MCU 处理 |
|---|---:|---|
| `ENTER_BOOTLOADER` | `0xB007` | 设置 Boot 请求并复位 |
| `SESSION_START` | `0xB101` | 初始化升级会话 |
| `ERASE_APP` | `0xB102` | 擦除 App Flash 区 |
| `TRANSFER_PACKET` | `0xB103` | 校验当前包 CRC 并写入 Flash |
| `VERIFY_IMAGE` | `0xB104` | 校验完整 App CRC |
| `ACTIVATE_IMAGE` | `0xB105` | 清除 Boot 请求并复位 |
| `ABORT` | `0xB1FF` | 中止升级并回到空闲 |

## 9. 平台适配函数

若你已有自己的 Flash/IAP 驱动，可以保留函数名，替换内部实现：

```c
void Bootload_Platform_Reset(void);
void Bootload_Platform_SetBootRequest(void);
void Bootload_Platform_ClearBootRequest(void);
uint8_t Bootload_Platform_IsBootRequested(void);
uint8_t Bootload_Platform_IsApplicationValid(void);
void Bootload_Platform_JumpToApplication(void);
uint8_t Bootload_Platform_EraseApplication(uint32_t image_size);
uint8_t Bootload_Platform_WriteApplication(uint32_t offset, const uint8_t *data, uint16_t length);
uint8_t Bootload_Platform_VerifyApplication(uint32_t image_size, uint32_t expected_crc);
void Bootload_Platform_ActivateApplication(void);
```

`bootload_mcu.c` 只保留 Bootload 协议状态机，不直接依赖 HAL。

`bootload_port_stm32f407.c` 提供基于 STM32 HAL 的默认平台实现，并使用 `BOOTLOAD_WEAK` 修饰。Keil/ARMCC/ARMCLANG 工程中 `BOOTLOAD_WEAK` 会展开为 `__weak`；GCC 工程中会展开为 `__attribute__((weak))`。

如果你的工程里需要自定义 Flash 分区、Boot 标志存储方式或复位前延时，可以在其他 `.c` 文件中实现同名强函数覆盖默认 weak 实现。若仍出现 multiply defined，请先执行一次 Clean/Rebuild，清掉旧的 `.o` 文件。

新增的 `Bootload_Platform_BeforeReset` 会在延迟复位前调用，默认 HAL 实现为 `HAL_Delay(20)`，用于给 W5500 发送 Modbus 写寄存器响应留出时间。

## 10. 注意事项

- App 工程的 `SCB->VTOR` 必须配置为 `0x08020000`。
- App 链接地址必须从 `0x08020000` 开始。
- Bootloader 和 App 的中断向量表不能重叠。
- 固件 CRC 与上位机保持一致，使用标准 CRC32 多项式 `0xEDB88320`。
- 当前 Boot 请求标志参考实现放在 SRAM 固定地址，量产建议改为 RTC Backup Register 或 Backup SRAM。
