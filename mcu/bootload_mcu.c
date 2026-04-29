#include "bootload_mcu.h"
#include <string.h>

static uint16_t *s_holding_regs;
static uint16_t s_holding_count;
static uint32_t s_expected_packet_index;
static uint32_t s_written_size;
static uint8_t s_reset_requested;

static uint8_t s_packet_buf[BOOT_PACKET_BYTES_MAX];

/**
 * @brief Combine two 16-bit Modbus registers into one unsigned 32-bit value.
 * @param high High 16 bits.
 * @param low Low 16 bits.
 * @return Combined 32-bit value.
 */
static uint32_t make_u32(uint16_t high, uint16_t low)
{
    return (((uint32_t)high) << 16) | (uint32_t)low;
}

/**
 * @brief Safely write one holding-register mirror value.
 * @param addr Zero-based holding register address.
 * @param value Value to store.
 * @note Writes outside the registered holding-register array are ignored.
 */
static void set_reg(uint16_t addr, uint16_t value)
{
    if (s_holding_regs && addr < s_holding_count)
    {
        s_holding_regs[addr] = value;
    }
}

/**
 * @brief Safely read one holding-register mirror value.
 * @param addr Zero-based holding register address.
 * @return Register value, or 0 if the address is outside the configured array.
 */
static uint16_t get_reg(uint16_t addr)
{
    if (s_holding_regs && addr < s_holding_count)
    {
        return s_holding_regs[addr];
    }
    return 0;
}

/**
 * @brief Update the Bootload status register.
 * @param status One of the BOOT_STATUS_* values.
 */
static void set_status(uint16_t status)
{
    set_reg(BOOT_REG_STATUS, status);
}

/**
 * @brief Update the Bootload error register and enter ERROR state if needed.
 * @param error One of the BOOT_ERR_* values. BOOT_ERR_NONE clears the error only.
 */
static void set_error(uint16_t error)
{
    set_reg(BOOT_REG_ERROR, error);
    if (error != BOOT_ERR_NONE)
    {
        set_status(BOOT_STATUS_ERROR);
    }
}

/**
 * @brief Calculate CRC32 using the standard Ethernet/ZIP polynomial.
 * @param crc Initial CRC seed, normally 0 for a fresh calculation.
 * @param data Pointer to bytes to include in the checksum.
 * @param len Number of valid bytes in data.
 * @return Final CRC32 value.
 */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t bit;

    crc = ~crc;
    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
        {
            if (crc & 1U)
            {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

/**
 * @brief Test whether a single register address was included in a write request.
 * @param start_addr First register written by the Modbus request.
 * @param quantity Number of registers written by the Modbus request.
 * @param addr Register address to test.
 * @return 1 if addr is inside the range, otherwise 0.
 */
static uint8_t addr_in_range(uint16_t start_addr, uint16_t quantity, uint16_t addr)
{
    return (addr >= start_addr) && (addr < (uint16_t)(start_addr + quantity));
}

/**
 * @brief Initialize a firmware download session after metadata registers are set.
 * @details Resets packet counters and validates that IMAGE_SIZE is non-zero.
 */
static void command_session_start(void)
{
    uint32_t image_size = make_u32(
        get_reg(BOOT_REG_IMAGE_SIZE_H),
        get_reg(BOOT_REG_IMAGE_SIZE_L));

    if (image_size == 0U)
    {
        set_error(BOOT_ERR_PACKET_LENGTH);
        return;
    }

    s_expected_packet_index = 0U;
    s_written_size = 0U;
    set_error(BOOT_ERR_NONE);
    set_status(BOOT_STATUS_READY);
}

/**
 * @brief Erase the configured application Flash region.
 * @details The platform layer decides which sectors/pages are affected based on
 *          the image size previously written by the host.
 */
static void command_erase_app(void)
{
    uint32_t image_size = make_u32(
        get_reg(BOOT_REG_IMAGE_SIZE_H),
        get_reg(BOOT_REG_IMAGE_SIZE_L));

    set_status(BOOT_STATUS_BUSY);
    if (Bootload_Platform_EraseApplication(image_size))
    {
        s_expected_packet_index = 0U;
        s_written_size = 0U;
        set_error(BOOT_ERR_NONE);
        set_status(BOOT_STATUS_READY);
    }
    else
    {
        set_error(BOOT_ERR_FLASH_ERASE);
    }
}

/**
 * @brief Validate and write the current firmware data packet.
 * @details Reads packet metadata and data from holding registers, checks packet
 *          order and packet CRC, then asks the platform layer to program Flash at
 *          the current image offset.
 */
static void command_transfer_packet(void)
{
    uint16_t i;
    uint32_t packet_index = make_u32(
        get_reg(BOOT_REG_PACKET_INDEX_H),
        get_reg(BOOT_REG_PACKET_INDEX_L));
    uint16_t packet_len = get_reg(BOOT_REG_PACKET_LENGTH);
    uint32_t expected_crc = make_u32(
        get_reg(BOOT_REG_PACKET_CRC_H),
        get_reg(BOOT_REG_PACKET_CRC_L));
    uint32_t actual_crc;

    if (packet_index != s_expected_packet_index)
    {
        set_error(BOOT_ERR_PACKET_INDEX);
        return;
    }

    if ((packet_len == 0U) || (packet_len > BOOT_PACKET_BYTES_MAX))
    {
        set_error(BOOT_ERR_PACKET_LENGTH);
        return;
    }

    for (i = 0; i < packet_len; i++)
    {
        uint16_t reg_value = get_reg((uint16_t)(BOOT_REG_DATA_START + (i / 2U)));
        if ((i & 1U) == 0U)
        {
            s_packet_buf[i] = (uint8_t)(reg_value >> 8);
        }
        else
        {
            s_packet_buf[i] = (uint8_t)(reg_value & 0xFFU);
        }
    }

    actual_crc = crc32_update(0U, s_packet_buf, packet_len);
    if (actual_crc != expected_crc)
    {
        set_error(BOOT_ERR_PACKET_CRC);
        return;
    }

    set_status(BOOT_STATUS_BUSY);
    if (Bootload_Platform_WriteApplication(s_written_size, s_packet_buf, packet_len))
    {
        s_written_size += packet_len;
        s_expected_packet_index++;
        set_error(BOOT_ERR_NONE);
        set_status(BOOT_STATUS_PACKET_OK);
    }
    else
    {
        set_error(BOOT_ERR_FLASH_WRITE);
    }
}

/**
 * @brief Verify the complete application image against the expected CRC32.
 * @details The platform layer reads Flash directly and compares against the
 *          IMAGE_CRC registers provided by the host.
 */
static void command_verify_image(void)
{
    uint32_t image_size = make_u32(
        get_reg(BOOT_REG_IMAGE_SIZE_H),
        get_reg(BOOT_REG_IMAGE_SIZE_L));
    uint32_t image_crc = make_u32(
        get_reg(BOOT_REG_IMAGE_CRC_H),
        get_reg(BOOT_REG_IMAGE_CRC_L));

    set_status(BOOT_STATUS_BUSY);
    if (Bootload_Platform_VerifyApplication(image_size, image_crc))
    {
        set_error(BOOT_ERR_NONE);
        set_status(BOOT_STATUS_VERIFY_OK);
    }
    else
    {
        set_error(BOOT_ERR_IMAGE_CRC);
    }
}

/**
 * @brief Mark the newly written image active and reset the MCU.
 * @details The default STM32F407 port clears the boot request flag, so the next
 *          boot can jump to the application if the vector table is valid.
 */
static void command_activate_image(void)
{
    Bootload_Platform_ActivateApplication();
    Bootload_Platform_ClearBootRequest();
    Bootload_RequestReset();
}

/**
 * @brief Handle App-mode request to enter Bootloader mode.
 * @details Sets a platform boot-request marker and resets the MCU. In products
 *          that must acknowledge Modbus before reset, delay the reset in the
 *          platform layer or schedule it from the main loop.
 */
static void command_enter_bootloader(void)
{
    Bootload_Platform_SetBootRequest();
    set_status(BOOT_STATUS_BUSY);
    Bootload_RequestReset();
}

/**
 * @brief Dispatch one BOOT_COMMAND register value to the matching handler.
 * @param command Command word written by the host.
 */
static void handle_command(uint16_t command)
{
    switch (command)
    {
    case BOOT_CMD_ENTER_BOOTLOADER:
        command_enter_bootloader();
        break;

    case BOOT_CMD_SESSION_START:
        command_session_start();
        break;

    case BOOT_CMD_ERASE_APP:
        command_erase_app();
        break;

    case BOOT_CMD_TRANSFER_PACKET:
        command_transfer_packet();
        break;

    case BOOT_CMD_VERIFY_IMAGE:
        command_verify_image();
        break;

    case BOOT_CMD_ACTIVATE_IMAGE:
        command_activate_image();
        break;

    case BOOT_CMD_ABORT:
        s_expected_packet_index = 0U;
        s_written_size = 0U;
        set_error(BOOT_ERR_NONE);
        set_status(BOOT_STATUS_IDLE);
        break;

    default:
        set_error(BOOT_ERR_INVALID_COMMAND);
        break;
    }
}

/**
 * @brief Bind the Bootload state machine to the Modbus holding-register array.
 * @param holding_regs Pointer to the global holding-register mirror.
 * @param holding_count Number of registers available in the mirror.
 */
void Bootload_Init(uint16_t *holding_regs, uint16_t holding_count)
{
    s_holding_regs = holding_regs;
    s_holding_count = holding_count;
    s_expected_packet_index = 0U;
    s_written_size = 0U;
    s_reset_requested = 0U;
    memset(s_packet_buf, 0, sizeof(s_packet_buf));

    set_reg(BOOT_REG_COMMAND, 0U);
    set_error(BOOT_ERR_NONE);
    set_status(BOOT_STATUS_READY);
}

/**
 * @brief Periodic Bootloader task.
 * @details If there is no pending boot request and the application vector table
 *          looks valid, this function jumps to the application.
 */
void Bootload_Task(void)
{
    if (s_reset_requested)
    {
        Bootload_Platform_BeforeReset();
        Bootload_Platform_Reset();
        while (1)
        {
        }
    }

    if (!Bootload_Platform_IsBootRequested() && Bootload_Platform_IsApplicationValid())
    {
        Bootload_Platform_JumpToApplication();
    }
}

/**
 * @brief Notify Bootload logic that holding registers were written.
 * @param start_addr First written holding-register address.
 * @param quantity Number of written holding registers.
 * @details When the write range includes BOOT_REG_COMMAND, the command value is
 *          dispatched immediately.
 */
void Bootload_OnHoldingWrite(uint16_t start_addr, uint16_t quantity)
{
    if (addr_in_range(start_addr, quantity, BOOT_REG_COMMAND))
    {
        handle_command(get_reg(BOOT_REG_COMMAND));
    }
}

/**
 * @brief Schedule a reset to be performed from Bootload_Task.
 * @details This avoids resetting inside the Modbus write handler before the TCP
 *          server has a chance to send the write response.
 */
void Bootload_RequestReset(void)
{
    s_reset_requested = 1U;
}
