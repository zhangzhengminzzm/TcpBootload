#include "bootload_mcu.h"

#if defined(STM32F407xx) || defined(STM32F40_41xxx) || defined(USE_HAL_DRIVER)

#include "stm32f4xx_hal.h"

#define BOOT_APP_START_ADDR         0x08020000UL
#define BOOT_APP_END_ADDR           0x08100000UL
#define BOOT_APP_MAX_SIZE           (BOOT_APP_END_ADDR - BOOT_APP_START_ADDR)
#define BOOT_REQUEST_MAGIC          0xB007B007UL
#define BOOT_REQUEST_MAGIC_ADDR     0x2001FFF0UL

typedef void (*app_entry_t)(void);

/**
 * @brief Calculate CRC32 over bytes stored in memory or Flash.
 * @param crc Initial CRC seed, normally 0 for full-image verification.
 * @param data Pointer to data bytes.
 * @param len Number of valid bytes.
 * @return Final CRC32 value using polynomial 0xEDB88320.
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
 * @brief Convert an STM32F407 Flash address to the HAL sector identifier.
 * @param address Absolute Flash address.
 * @return FLASH_SECTOR_x value used by HAL_FLASHEx_Erase.
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
 * @brief Validate that a byte range is fully inside the application partition.
 * @param address Absolute start address.
 * @param length Number of bytes in the range.
 * @return 1 if the range is valid for App Flash, otherwise 0.
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
 * @brief Optional delay before executing a scheduled reset.
 * @details Gives the W5500/TCP server a short window to send the Modbus write
 *          response before the MCU resets into Bootloader or App mode.
 */
BOOTLOAD_WEAK void Bootload_Platform_BeforeReset(void)
{
    HAL_Delay(20U);
}

/**
 * @brief Reset the STM32F407 after a Bootload state transition.
 */
BOOTLOAD_WEAK void Bootload_Platform_Reset(void)
{
    HAL_NVIC_SystemReset();
}

/**
 * @brief Store the volatile boot-request marker before resetting from App mode.
 * @note This sample uses SRAM near the top of RAM. For production, RTC backup
 *       register or backup SRAM is safer across reset modes.
 */
BOOTLOAD_WEAK void Bootload_Platform_SetBootRequest(void)
{
    *((volatile uint32_t *)BOOT_REQUEST_MAGIC_ADDR) = BOOT_REQUEST_MAGIC;
}

/**
 * @brief Clear the boot-request marker so the next boot can enter the App.
 */
BOOTLOAD_WEAK void Bootload_Platform_ClearBootRequest(void)
{
    *((volatile uint32_t *)BOOT_REQUEST_MAGIC_ADDR) = 0UL;
}

/**
 * @brief Check whether the previous firmware requested Bootloader mode.
 * @return 1 if the boot magic is present, otherwise 0.
 */
BOOTLOAD_WEAK uint8_t Bootload_Platform_IsBootRequested(void)
{
    return (*((volatile uint32_t *)BOOT_REQUEST_MAGIC_ADDR) == BOOT_REQUEST_MAGIC) ? 1U : 0U;
}

/**
 * @brief Validate the application vector table before jumping.
 * @return 1 if initial SP and reset vector are inside expected memory ranges.
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
 * @brief Deinitialize HAL state and jump to the application reset handler.
 * @details Interrupts are disabled, VTOR is moved to the App vector table, MSP is
 *          loaded from the App image, and then the App reset handler is called.
 */
BOOTLOAD_WEAK void Bootload_Platform_JumpToApplication(void)
{
    uint32_t app_sp = *((volatile uint32_t *)BOOT_APP_START_ADDR);
    uint32_t app_reset = *((volatile uint32_t *)(BOOT_APP_START_ADDR + 4UL));
    app_entry_t app_entry = (app_entry_t)app_reset;

    __disable_irq();
    HAL_RCC_DeInit();
    HAL_DeInit();
    SCB->VTOR = BOOT_APP_START_ADDR;
    __set_MSP(app_sp);
    app_entry();
}

/**
 * @brief Erase all STM32F407 Flash sectors needed by the incoming App image.
 * @param image_size Firmware image size in bytes.
 * @return 1 on successful erase, otherwise 0.
 */
BOOTLOAD_WEAK uint8_t Bootload_Platform_EraseApplication(uint32_t image_size)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0U;
    uint32_t first_sector;
    uint32_t last_sector;

    if (!address_is_in_app(BOOT_APP_START_ADDR, image_size))
    {
        return 0U;
    }

    first_sector = sector_from_address(BOOT_APP_START_ADDR);
    last_sector = sector_from_address(BOOT_APP_START_ADDR + image_size - 1U);

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = first_sector;
    erase.NbSectors = (last_sector - first_sector) + 1U;

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
 * @brief Program one validated firmware packet into the application partition.
 * @param offset Byte offset from BOOT_APP_START_ADDR.
 * @param data Packet bytes from the Modbus data buffer.
 * @param length Number of valid bytes in the packet.
 * @return 1 if all words were programmed successfully, otherwise 0.
 */
BOOTLOAD_WEAK uint8_t Bootload_Platform_WriteApplication(uint32_t offset, const uint8_t *data, uint16_t length)
{
    uint32_t address = BOOT_APP_START_ADDR + offset;
    uint32_t i;

    if (!address_is_in_app(address, length))
    {
        return 0U;
    }

    HAL_FLASH_Unlock();
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
    HAL_FLASH_Lock();
    return 1U;
}

/**
 * @brief Verify the programmed image by calculating CRC32 from Flash.
 * @param image_size Number of bytes to verify from BOOT_APP_START_ADDR.
 * @param expected_crc CRC32 sent by the host.
 * @return 1 when CRC matches, otherwise 0.
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
 * @brief Mark the verified application as active.
 * @details Current sample only clears the boot-request marker. Add persistent
 *          image-valid flags here if your product needs A/B or rollback support.
 */
BOOTLOAD_WEAK void Bootload_Platform_ActivateApplication(void)
{
    Bootload_Platform_ClearBootRequest();
}

#endif
