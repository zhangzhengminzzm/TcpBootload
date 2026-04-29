#ifndef __BOOTLOAD_MCU_H__
#define __BOOTLOAD_MCU_H__

#include <stdint.h>

#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define BOOTLOAD_WEAK __weak
#elif defined(__GNUC__)
#define BOOTLOAD_WEAK __attribute__((weak))
#elif defined(__ICCARM__)
#define BOOTLOAD_WEAK __weak
#else
#define BOOTLOAD_WEAK
#endif

#define BOOTLOAD_MODBUS_UNIT_ID              1U

#define BOOT_REG_COMMAND                     0x0100U
#define BOOT_REG_STATUS                      0x0101U
#define BOOT_REG_ERROR                       0x0102U
#define BOOT_REG_SESSION_ID                  0x0103U
#define BOOT_REG_PACKET_INDEX_H              0x0104U
#define BOOT_REG_PACKET_INDEX_L              0x0105U
#define BOOT_REG_PACKET_LENGTH               0x0106U
#define BOOT_REG_PACKET_CRC_H                0x0107U
#define BOOT_REG_PACKET_CRC_L                0x0108U
#define BOOT_REG_IMAGE_SIZE_H                0x0109U
#define BOOT_REG_IMAGE_SIZE_L                0x010AU
#define BOOT_REG_IMAGE_CRC_H                 0x010BU
#define BOOT_REG_IMAGE_CRC_L                 0x010CU
#define BOOT_REG_DATA_START                  0x0200U
#define BOOT_REG_DATA_COUNT                  120U
#define BOOT_PACKET_BYTES_MAX                (BOOT_REG_DATA_COUNT * 2U)

#define BOOT_FLAG_UPGRADE_MODE               0xB007B007UL
#define BOOT_FLAG_WORKING_MODE               0x000000AAUL

#define BOOT_CMD_ENTER_BOOTLOADER            0xB007U
#define BOOT_CMD_SESSION_START               0xB101U
#define BOOT_CMD_ERASE_APP                   0xB102U
#define BOOT_CMD_TRANSFER_PACKET             0xB103U
#define BOOT_CMD_VERIFY_IMAGE                0xB104U
#define BOOT_CMD_ACTIVATE_IMAGE              0xB105U
#define BOOT_CMD_ABORT                       0xB1FFU

#define BOOT_STATUS_IDLE                     0x0000U
#define BOOT_STATUS_BUSY                     0x0001U
#define BOOT_STATUS_READY                    0x0002U
#define BOOT_STATUS_PACKET_OK                0x0003U
#define BOOT_STATUS_VERIFY_OK                0x0004U
#define BOOT_STATUS_ERROR                    0x8000U

#define BOOT_ERR_NONE                        0x0000U
#define BOOT_ERR_INVALID_COMMAND             0x0001U
#define BOOT_ERR_INVALID_SESSION             0x0002U
#define BOOT_ERR_PACKET_INDEX                0x0003U
#define BOOT_ERR_PACKET_LENGTH               0x0004U
#define BOOT_ERR_PACKET_CRC                  0x0005U
#define BOOT_ERR_FLASH_ERASE                 0x0006U
#define BOOT_ERR_FLASH_WRITE                 0x0007U
#define BOOT_ERR_IMAGE_CRC                   0x0008U
#define BOOT_ERR_VERSION_REJECTED            0x0009U

/**
 * @brief 初始化 Bootload 协议状态机，并绑定 Modbus 保持寄存器镜像区。
 * @param holding_regs Modbus TCP 使用的保持寄存器数组指针。
 * @param holding_count holding_regs 中可访问的寄存器数量。
 */
void Bootload_Init(uint16_t *holding_regs, uint16_t holding_count);

/**
 * @brief Bootload 周期任务，需要在主循环中持续调用。
 * @details 当没有 Boot 请求且 APP 向量表有效时，本函数会跳转到 APP。
 */
void Bootload_Task(void);

/**
 * @brief 通知 Bootload 逻辑：保持寄存器已经被 Modbus 写入。
 * @param start_addr 本次写入的第一个保持寄存器地址。
 * @param quantity 本次写入的保持寄存器数量。
 */
void Bootload_OnHoldingWrite(uint16_t start_addr, uint16_t quantity);

/**
 * @brief 请求在 Bootload_Task 中执行延时复位。
 * @details 用于需要先回复 Modbus 写命令，再进行 MCU 复位的场景。
 */
void Bootload_RequestReset(void);

/**
 * @brief 执行 MCU 复位。
 */
void Bootload_Platform_Reset(void);

/**
 * @brief 复位前的平台钩子函数。
 * @details HAL 移植层可在此延时数毫秒，确保 W5500 有时间把 TCP 应答发出后再复位。
 */
void Bootload_Platform_BeforeReset(void);

/**
 * @brief 向 Flash boot_flag 写入升级模式，表示下次启动应停留在 Bootloader。
 */
void Bootload_Platform_SetBootRequest(void);

/**
 * @brief 向 Flash boot_flag 写入工作模式，允许下次启动进入 APP。
 */
void Bootload_Platform_ClearBootRequest(void);

/**
 * @brief 查询 Flash boot_flag 是否要求停留在 Bootloader。
 * @return 返回 1 表示需要停留在 Bootloader，返回 0 表示不需要。
 */
uint8_t Bootload_Platform_IsBootRequested(void);

/**
 * @brief 检查 APP 镜像向量表是否合法。
 * @return 返回 1 表示 APP 可跳转，返回 0 表示 APP 无效。
 */
uint8_t Bootload_Platform_IsApplicationValid(void);

/**
 * @brief 从 Bootloader 跳转到 APP 固件。
 */
void Bootload_Platform_JumpToApplication(void);

/**
 * @brief 擦除即将写入 APP 镜像的 Flash 区域。
 * @param image_size 上位机下发的固件镜像大小，单位为字节。
 * @return 返回 1 表示擦除成功，返回 0 表示擦除失败。
 */
uint8_t Bootload_Platform_EraseApplication(uint32_t image_size);

/**
 * @brief 将一个升级数据包写入 APP Flash 区域。
 * @param offset 相对 APP 起始地址的字节偏移。
 * @param data 升级数据包缓存指针。
 * @param length data 中有效数据长度，单位为字节。
 * @return 返回 1 表示写入成功，返回 0 表示写入失败。
 */
uint8_t Bootload_Platform_WriteApplication(uint32_t offset, const uint8_t *data, uint16_t length);

/**
 * @brief 对已经写入的完整 APP 镜像进行 CRC32 校验。
 * @param image_size 需要校验的 APP 镜像长度，单位为字节。
 * @param expected_crc 上位机给出的期望 CRC32 值。
 * @return 返回 1 表示校验成功，返回 0 表示校验失败。
 */
uint8_t Bootload_Platform_VerifyApplication(uint32_t image_size, uint32_t expected_crc);

/**
 * @brief 将已经校验通过的 APP 镜像标记为可启动。
 */
void Bootload_Platform_ActivateApplication(void);

#endif
