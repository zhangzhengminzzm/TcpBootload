#include "bootload_mcu.h"

#if defined(STM32F407xx) || defined(STM32F40_41xxx) || defined(USE_HAL_DRIVER)

#include "stm32f4xx_hal.h"

#ifndef BOOT_APP_START_ADDR
#define BOOT_APP_START_ADDR         0x08020000UL
#endif

#ifndef BOOT_FLAG_ADDR
#define BOOT_FLAG_ADDR              0x080E0000UL
#endif

#ifndef BOOT_APP_END_ADDR
#define BOOT_APP_END_ADDR           BOOT_FLAG_ADDR
#endif

#define BOOT_APP_MAX_SIZE           (BOOT_APP_END_ADDR - BOOT_APP_START_ADDR)

typedef void (*app_entry_t)(void);

/**
 * @brief 对内存或 Flash 中的字节数据计算 CRC32。
 * @param crc 初始 CRC 种子，完整镜像校验时通常传入 0。
 * @param data 数据起始地址。
 * @param len 有效数据长度，单位为字节。
 * @return 使用 0xEDB88320 多项式计算得到的 CRC32。
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
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return ~crc;
}

/**
 * @brief 将 STM32F407 Flash 地址转换为 HAL 扇区编号。
 * @param address 绝对 Flash 地址。
 * @return HAL_FLASHEx_Erase 使用的 FLASH_SECTOR_x 扇区值。
 */
static uint32_t sector_from_address(uint32_t address)
{
    if (address < 0x08004000UL) return FLASH_SECTOR_0;
    if (address < 0x08008000UL) return FLASH_SECTOR_1;
    if (address < 0x0800C000UL) return FLASH_SECTOR_2;
    if (address < 0x08010000UL) return FLASH_SECTOR_3;
    if (address < 0x08020000UL) return FLASH_SECTOR_4;
    if (address < 0x08040000UL) return FLASH_SECTOR_5;
    if (address < 0x08060000UL) return FLASH_SECTOR_6;
    if (address < 0x08080000UL) return FLASH_SECTOR_7;
    if (address < 0x080A0000UL) return FLASH_SECTOR_8;
    if (address < 0x080C0000UL) return FLASH_SECTOR_9;
    if (address < 0x080E0000UL) return FLASH_SECTOR_10;
    return FLASH_SECTOR_11;
}

/**
 * @brief 检查指定字节范围是否完全位于 APP Flash 分区内。
 * @param address 待检查范围的绝对起始地址。
 * @param length 待检查范围的字节长度。
 * @return 返回 1 表示范围合法，返回 0 表示范围越界或长度无效。
 */
static uint8_t address_is_in_app(uint32_t address, uint32_t length)
{
    if (length == 0U)
    {
        return 0U;
    }
    if (address < BOOT_APP_START_ADDR)
    {
        return 0U;
    }
    if ((address + length) > BOOT_APP_END_ADDR)
    {
        return 0U;
    }
    return 1U;
}

/**
 * @brief 擦除 Boot 标识所在 Flash 扇区，并写入新的模式标识。
 * @param flag_value 需要写入 BOOT_FLAG_ADDR 的 32 位模式值。
 * @return 返回 1 表示标识写入成功，返回 0 表示擦除或编程失败。
 * @note BOOT_FLAG_ADDR 所在扇区必须独立预留，不能和 Bootloader 或 App 代码共用。
 */
static uint8_t write_boot_flag(uint32_t flag_value)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0U;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = sector_from_address(BOOT_FLAG_ADDR);
    erase.NbSectors = 1U;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGSERR | FLASH_FLAG_WRPERR);

    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 0U;
    }

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOT_FLAG_ADDR, flag_value) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 0U;
    }

    HAL_FLASH_Lock();
    return 1U;
}

/**
 * @brief 读取 Flash 中保存的 Boot 工作模式标识。
 * @return BOOT_FLAG_ADDR 地址处保存的 32 位模式值。
 */
static uint32_t read_boot_flag(void)
{
    return *((volatile uint32_t *)BOOT_FLAG_ADDR);
}

/**
 * @brief 执行计划复位前的可选延时钩子。
 * @details 给 W5500/TCP 服务预留短暂时间发送 Modbus 写响应，然后 MCU 再复位进入 Bootloader 或 APP。
 */
BOOTLOAD_WEAK void Bootload_Platform_BeforeReset(void)
{
    HAL_Delay(20U);
}

/**
 * @brief 在 Bootload 状态切换后复位 STM32F407。
 */
BOOTLOAD_WEAK void Bootload_Platform_Reset(void)
{
    HAL_NVIC_SystemReset();
}

/**
 * @brief 从 APP 模式复位前写入 Flash 升级模式标识。
 * @note 写入 BOOT_FLAG_UPGRADE_MODE 后，Bootloader 复位启动会停留在升级模式。
 */
BOOTLOAD_WEAK void Bootload_Platform_SetBootRequest(void)
{
    (void)write_boot_flag(BOOT_FLAG_UPGRADE_MODE);
}

/**
 * @brief 将 Flash Boot 标识切换为 APP 工作模式。
 * @note 工作模式值为 BOOT_FLAG_WORKING_MODE，也就是 0x000000AA。
 */
BOOTLOAD_WEAK void Bootload_Platform_ClearBootRequest(void)
{
    (void)write_boot_flag(BOOT_FLAG_WORKING_MODE);
}

/**
 * @brief 查询 Flash Boot 标识是否要求停留在 Bootloader。
 * @return 返回 1 表示不是工作模式，需要停留 Bootloader；返回 0 表示 APP 工作模式。
 */
BOOTLOAD_WEAK uint8_t Bootload_Platform_IsBootRequested(void)
{
    return (read_boot_flag() != BOOT_FLAG_WORKING_MODE) ? 1U : 0U;
}

/**
 * @brief 跳转 APP 前检查 APP 向量表是否合法。
 * @return 返回 1 表示初始栈指针和复位向量均位于期望地址范围内。
 */
BOOTLOAD_WEAK uint8_t Bootload_Platform_IsApplicationValid(void)
{
    uint32_t app_sp = *((volatile uint32_t *)BOOT_APP_START_ADDR);
    uint32_t app_reset = *((volatile uint32_t *)(BOOT_APP_START_ADDR + 4UL));

    if ((app_sp < 0x20000000UL) || (app_sp > 0x20020000UL))
    {
        return 0U;
    }
    if ((app_reset < BOOT_APP_START_ADDR) || (app_reset >= BOOT_APP_END_ADDR))
    {
        return 0U;
    }
    return 1U;
}

/**
 * @brief 反初始化 HAL 状态并跳转到 APP 复位入口。
 * @details 跳转前会关闭中断、切换 VTOR 到 APP 向量表、装载 APP 初始 MSP，
 *          最后调用 APP 复位处理函数。
 */
BOOTLOAD_WEAK void Bootload_Platform_JumpToApplication(void)
{
    uint32_t app_sp = *((volatile uint32_t *)BOOT_APP_START_ADDR);
    uint32_t app_reset = *((volatile uint32_t *)(BOOT_APP_START_ADDR + 4UL));
    app_entry_t app_entry = (app_entry_t)app_reset;

    /* 步骤1：关闭中断，避免 Bootloader 外设中断在 APP 环境中继续触发。 */
    __disable_irq();

    /* 步骤2：反初始化 RCC/HAL，尽量清理 Bootloader 初始化过的硬件状态。 */
    HAL_RCC_DeInit();
    HAL_DeInit();

    /* 步骤3：切换向量表和主栈指针，准备进入 APP 运行环境。 */
    SCB->VTOR = BOOT_APP_START_ADDR;
    __set_MSP(app_sp);

    /* 步骤4：跳转到 APP 复位处理函数，执行权正式交给 APP。 */
    app_entry();
}

/**
 * @brief 擦除待写入 APP 镜像覆盖到的 STM32F407 Flash 扇区。
 * @param image_size 固件镜像大小，单位为字节。
 * @return 返回 1 表示擦除成功，返回 0 表示擦除失败。
 */
BOOTLOAD_WEAK uint8_t Bootload_Platform_EraseApplication(uint32_t image_size)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0U;
    uint32_t first_sector;
    uint32_t last_sector;

    /* 步骤1：先校验镜像大小对应的地址范围，防止擦除 Bootloader 或越界 Flash。 */
    if (!address_is_in_app(BOOT_APP_START_ADDR, image_size))
    {
        return 0U;
    }

    /* 步骤2：根据 APP 起始地址和镜像尾地址计算需要擦除的首尾扇区。 */
    first_sector = sector_from_address(BOOT_APP_START_ADDR);
    last_sector = sector_from_address(BOOT_APP_START_ADDR + image_size - 1U);

    /* 步骤3：填充 HAL 扇区擦除参数，电压范围按 STM32F4 常用 2.7V~3.6V 配置。 */
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = first_sector;
    erase.NbSectors = (last_sector - first_sector) + 1U;

    /* 步骤4：解锁 Flash、执行擦除、再重新加锁，失败时立即返回错误。 */
    HAL_FLASH_Unlock();
    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 0U;
    }
    HAL_FLASH_Lock();
    return 1U;
}

/**
 * @brief 将一个已校验的固件数据包写入 APP 分区。
 * @param offset 相对 BOOT_APP_START_ADDR 的字节偏移。
 * @param data 来自 Modbus 数据缓冲区的包数据。
 * @param length 当前包有效数据长度，单位为字节。
 * @return 返回 1 表示所有字写入成功，返回 0 表示写入失败。
 */
BOOTLOAD_WEAK uint8_t Bootload_Platform_WriteApplication(uint32_t offset, const uint8_t *data, uint16_t length)
{
    uint32_t address = BOOT_APP_START_ADDR + offset;
    uint32_t i;

    /* 步骤1：计算目标写入地址，并确认整包数据位于 APP 分区范围内。 */
    if (!address_is_in_app(address, length))
    {
        return 0U;
    }

    /* 步骤2：解锁 Flash，准备按 STM32F4 的 word 编程粒度写入。 */
    HAL_FLASH_Unlock();

    /* 步骤3：每 4 字节组成一个 word；最后不足 4 字节时用 0xFF 补齐再写入。 */
    for (i = 0U; i < length; i += 4U)
    {
        uint32_t word = 0xFFFFFFFFUL;
        uint8_t copy_len = ((length - i) >= 4U) ? 4U : (uint8_t)(length - i);
        uint8_t *word_bytes = (uint8_t *)&word;
        uint8_t j;

        for (j = 0U; j < copy_len; j++)
        {
            word_bytes[j] = data[i + j];
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i, word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return 0U;
        }
    }

    /* 步骤4：写入完成后重新锁定 Flash，并向上层返回成功。 */
    HAL_FLASH_Lock();
    return 1U;
}

/**
 * @brief 从 Flash 读取已写入镜像并计算 CRC32 进行校验。
 * @param image_size 从 BOOT_APP_START_ADDR 开始校验的字节数。
 * @param expected_crc 上位机发送的期望 CRC32。
 * @return 返回 1 表示 CRC 匹配，返回 0 表示 CRC 不匹配或范围非法。
 */
BOOTLOAD_WEAK uint8_t Bootload_Platform_VerifyApplication(uint32_t image_size, uint32_t expected_crc)
{
    uint32_t actual_crc;

    if (!address_is_in_app(BOOT_APP_START_ADDR, image_size))
    {
        return 0U;
    }

    actual_crc = crc32_update(0U, (const uint8_t *)BOOT_APP_START_ADDR, image_size);
    return (actual_crc == expected_crc) ? 1U : 0U;
}

/**
 * @brief 将校验通过的 APP 标记为可启动。
 * @details 升级成功后写入 Flash 工作模式标识 0x000000AA，复位后 Bootloader
 *          检测到工作模式且 APP 向量表有效，就会跳转到 APP。
 */
BOOTLOAD_WEAK void Bootload_Platform_ActivateApplication(void)
{
    Bootload_Platform_ClearBootRequest();
}

#endif
