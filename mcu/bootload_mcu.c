#include "bootload_mcu.h"
#include <string.h>

static uint16_t *s_holding_regs;
static uint16_t s_holding_count;
static uint32_t s_expected_packet_index;
static uint32_t s_written_size;
static uint8_t s_reset_requested;

static uint8_t s_packet_buf[BOOT_PACKET_BYTES_MAX];

/**
 * @brief 将两个 16 位 Modbus 寄存器组合为一个 32 位无符号数。
 * @param high 32 位数据的高 16 位。
 * @param low 32 位数据的低 16 位。
 * @return 组合后的 32 位数据。
 */
static uint32_t make_u32(uint16_t high, uint16_t low)
{
    return (((uint32_t)high) << 16) | (uint32_t)low;
}

/**
 * @brief 安全写入一个保持寄存器镜像值。
 * @param addr 从 0 开始计数的保持寄存器地址。
 * @param value 需要写入的寄存器值。
 * @note 当地址超出已绑定的保持寄存器数组范围时，本函数会直接忽略。
 */
static void set_reg(uint16_t addr, uint16_t value)
{
    if (s_holding_regs && addr < s_holding_count)
    {
        s_holding_regs[addr] = value;
    }
}

/**
 * @brief 安全读取一个保持寄存器镜像值。
 * @param addr 从 0 开始计数的保持寄存器地址。
 * @return 读取到的寄存器值；地址越界时返回 0。
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
 * @brief 更新 Bootload 状态寄存器。
 * @param status BOOT_STATUS_* 状态值之一。
 */
static void set_status(uint16_t status)
{
    set_reg(BOOT_REG_STATUS, status);
}

/**
 * @brief 更新 Bootload 错误寄存器，必要时进入错误状态。
 * @param error BOOT_ERR_* 错误值之一；BOOT_ERR_NONE 仅用于清除错误码。
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
 * @brief 使用标准 Ethernet/ZIP 多项式计算 CRC32。
 * @param crc 初始 CRC 种子，新一轮计算通常传入 0。
 * @param data 参与校验的数据指针。
 * @param len data 中有效数据长度，单位为字节。
 * @return 计算得到的最终 CRC32 值。
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
 * @brief 判断某个寄存器地址是否包含在本次 Modbus 写入范围内。
 * @param start_addr Modbus 请求写入的第一个寄存器地址。
 * @param quantity Modbus 请求写入的寄存器数量。
 * @param addr 需要判断的目标寄存器地址。
 * @return 返回 1 表示目标地址在写入范围内，返回 0 表示不在范围内。
 */
static uint8_t addr_in_range(uint16_t start_addr, uint16_t quantity, uint16_t addr)
{
    return (addr >= start_addr) && (addr < (uint16_t)(start_addr + quantity));
}

/**
 * @brief 在镜像元数据寄存器写入完成后，初始化一次固件下载会话。
 * @details 本函数会检查 IMAGE_SIZE 是否非 0，并复位包序号和已写入长度计数器。
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
 * @brief 擦除 APP 固件所在的 Flash 区域。
 * @details 平台层会根据上位机写入的镜像大小，决定实际擦除哪些扇区或页。
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
 * @brief 校验并写入当前固件数据包。
 * @details 本函数从保持寄存器读取包元数据和包内容，按包序号、长度和 CRC32 校验后，
 *          再调用平台层把数据写入当前 APP Flash 偏移位置。
 */
static void command_transfer_packet(void)
{
    uint16_t i;
    /* 步骤1：从保持寄存器读取包序号、包长度和本包 CRC32。 */
    uint32_t packet_index = make_u32(
        get_reg(BOOT_REG_PACKET_INDEX_H),
        get_reg(BOOT_REG_PACKET_INDEX_L));
    uint16_t packet_len = get_reg(BOOT_REG_PACKET_LENGTH);
    uint32_t expected_crc = make_u32(
        get_reg(BOOT_REG_PACKET_CRC_H),
        get_reg(BOOT_REG_PACKET_CRC_L));
    uint32_t actual_crc;

    /* 步骤2：检查包序号是否等于 Bootload 当前期望序号，避免乱序或重复写入。 */
    if (packet_index != s_expected_packet_index)
    {
        set_error(BOOT_ERR_PACKET_INDEX);
        return;
    }

    /* 步骤3：检查包长度是否为有效值，并且不能超过保持寄存器数据区容量。 */
    if ((packet_len == 0U) || (packet_len > BOOT_PACKET_BYTES_MAX))
    {
        set_error(BOOT_ERR_PACKET_LENGTH);
        return;
    }

    /* 步骤4：将 Modbus 16 位寄存器中的高低字节拆包到连续字节缓存。 */
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

    /* 步骤5：对本包数据计算 CRC32，并与上位机下发的 CRC32 比较。 */
    actual_crc = crc32_update(0U, s_packet_buf, packet_len);
    if (actual_crc != expected_crc)
    {
        set_error(BOOT_ERR_PACKET_CRC);
        return;
    }

    /* 步骤6：CRC 通过后写入 Flash，并更新已写入长度和下一包期望序号。 */
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
 * @brief 使用期望 CRC32 校验完整 APP 镜像。
 * @details 平台层会直接读取 Flash 中的 APP 镜像，并与上位机写入的 IMAGE_CRC 比较。
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
 * @brief 将新写入镜像标记为可启动，并请求 MCU 复位。
 * @details 默认 STM32F407 移植层会把 Flash 中的 boot_flag 写为工作模式
 *          BOOT_FLAG_WORKING_MODE，也就是 0x000000AA。下次启动时，如果
 *          APP 向量表有效，Bootloader 将直接跳转到 APP。
 */
static void command_activate_image(void)
{
    Bootload_Platform_ActivateApplication();
    Bootload_RequestReset();
}

/**
 * @brief 处理 APP 模式下进入 Bootloader 的请求。
 * @details 写入平台 Boot 请求标识并请求复位。为了保证 Modbus 写命令能先应答，
 *          实际复位会由 Bootload_Task 在主循环中延后执行。
 */
static void command_enter_bootloader(void)
{
    Bootload_Platform_SetBootRequest();
    set_status(BOOT_STATUS_BUSY);
    Bootload_RequestReset();
}

/**
 * @brief 根据 BOOT_COMMAND 寄存器的命令字分发到对应处理函数。
 * @param command 上位机写入的 Bootload 命令字。
 */
static void handle_command(uint16_t command)
{
    /* 步骤1：按命令字进入对应流程，所有命令均来自保持寄存器 BOOT_REG_COMMAND。 */
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
        /* 步骤2：中止命令只复位会话计数器和状态，不主动擦除或改写 Flash。 */
        s_expected_packet_index = 0U;
        s_written_size = 0U;
        set_error(BOOT_ERR_NONE);
        set_status(BOOT_STATUS_IDLE);
        break;

    default:
        /* 步骤3：未知命令统一进入错误状态，方便上位机读取错误码定位问题。 */
        set_error(BOOT_ERR_INVALID_COMMAND);
        break;
    }
}

/**
 * @brief 将 Bootload 状态机绑定到 Modbus 保持寄存器数组。
 * @param holding_regs 全局保持寄存器镜像数组指针。
 * @param holding_count 保持寄存器镜像数组中的可用寄存器数量。
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
 * @brief Bootloader 周期任务。
 * @details 如果存在延时复位请求，则先执行复位；如果没有 Boot 请求且 APP 有效，则跳转到 APP。
 */
void Bootload_Task(void)
{
    if (s_reset_requested)
    {
        /* 步骤1：先执行复位前钩子，给 W5500/TCP 应答留出发送时间。 */
        Bootload_Platform_BeforeReset();
        Bootload_Platform_Reset();
        while (1)
        {
        }
    }

    /* 步骤2：无 Boot 请求且 APP 向量表有效时，直接切换到 APP 执行。 */
    if (!Bootload_Platform_IsBootRequested() && Bootload_Platform_IsApplicationValid())
    {
        Bootload_Platform_JumpToApplication();
    }
}

/**
 * @brief 通知 Bootload：保持寄存器发生了写入。
 * @param start_addr 本次写入的第一个保持寄存器地址。
 * @param quantity 本次写入的保持寄存器数量。
 * @details 当写入范围包含 BOOT_REG_COMMAND 时，会立即读取命令并执行对应流程。
 */
void Bootload_OnHoldingWrite(uint16_t start_addr, uint16_t quantity)
{
    if (addr_in_range(start_addr, quantity, BOOT_REG_COMMAND))
    {
        handle_command(get_reg(BOOT_REG_COMMAND));
    }
}

/**
 * @brief 调度一次由 Bootload_Task 执行的延时复位。
 * @details 避免在 Modbus 写寄存器回调内部立即复位，导致 TCP 写响应还未发送就断开。
 */
void Bootload_RequestReset(void)
{
    s_reset_requested = 1U;
}
