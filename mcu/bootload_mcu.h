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
 * @brief Initialize Bootload protocol state using the Modbus holding-register mirror.
 * @param holding_regs Pointer to the holding-register array used by Modbus TCP.
 * @param holding_count Number of valid entries in holding_regs.
 */
void Bootload_Init(uint16_t *holding_regs, uint16_t holding_count);

/**
 * @brief Run periodic Bootloader logic from the main loop.
 * @details Jumps to the App when no Boot request is pending and the App vector
 *          table is valid.
 */
void Bootload_Task(void);

/**
 * @brief Notify Bootload logic after holding registers are written.
 * @param start_addr First holding-register address written by Modbus.
 * @param quantity Number of registers written.
 */
void Bootload_OnHoldingWrite(uint16_t start_addr, uint16_t quantity);

/**
 * @brief Request a delayed platform reset from Bootload_Task.
 * @details Use this when a Modbus command should be acknowledged before reset.
 */
void Bootload_RequestReset(void);

/**
 * @brief Reset the MCU.
 */
void Bootload_Platform_Reset(void);

/**
 * @brief Optional platform hook called before delayed reset execution.
 * @details HAL ports may use this hook to delay a few milliseconds so TCP data
 *          has time to leave the W5500 before NVIC reset.
 */
void Bootload_Platform_BeforeReset(void);

/**
 * @brief Store a marker indicating the next boot should stay in Bootloader mode.
 */
void Bootload_Platform_SetBootRequest(void);

/**
 * @brief Clear the Bootloader request marker.
 */
void Bootload_Platform_ClearBootRequest(void);

/**
 * @brief Check whether Bootloader mode was requested.
 * @return 1 if Bootloader mode is requested, otherwise 0.
 */
uint8_t Bootload_Platform_IsBootRequested(void);

/**
 * @brief Check whether the application image has a valid vector table.
 * @return 1 if the application can be jumped to, otherwise 0.
 */
uint8_t Bootload_Platform_IsApplicationValid(void);

/**
 * @brief Jump from Bootloader to application firmware.
 */
void Bootload_Platform_JumpToApplication(void);

/**
 * @brief Erase the Flash range used by the incoming application image.
 * @param image_size Incoming firmware size in bytes.
 * @return 1 on success, otherwise 0.
 */
uint8_t Bootload_Platform_EraseApplication(uint32_t image_size);

/**
 * @brief Program one packet into application Flash.
 * @param offset Byte offset from the application base address.
 * @param data Pointer to packet bytes.
 * @param length Number of valid bytes in data.
 * @return 1 on success, otherwise 0.
 */
uint8_t Bootload_Platform_WriteApplication(uint32_t offset, const uint8_t *data, uint16_t length);

/**
 * @brief Verify the complete application image after programming.
 * @param image_size Number of application bytes to verify.
 * @param expected_crc Host-provided CRC32 value.
 * @return 1 when verification succeeds, otherwise 0.
 */
uint8_t Bootload_Platform_VerifyApplication(uint32_t image_size, uint32_t expected_crc);

/**
 * @brief Mark the verified application image as active.
 */
void Bootload_Platform_ActivateApplication(void);

#endif
