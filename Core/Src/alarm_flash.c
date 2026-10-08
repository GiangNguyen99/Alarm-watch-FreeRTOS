#include "alarm_flash.h"

#define ALARM_FLASH_ADDR    0x08060000U
#define ALARM_FLASH_SECTOR  FLASH_SECTOR_7

#define ALARM_MAGIC         0x414C524DU
#define ALARM_VERSION       1U


uint8_t AlarmFlash_Load(uint8_t *hour,
                        uint8_t *minute,
                        uint8_t *enabled)
{
    if (!hour || !minute || !enabled)
        return 0;

    uint32_t magic =
        *(const volatile uint32_t *)ALARM_FLASH_ADDR;

    uint32_t data =
        *(const volatile uint32_t *)(ALARM_FLASH_ADDR + 4U);

    if (magic != ALARM_MAGIC)
        return 0;

    uint8_t h = (uint8_t)(data & 0xFFU);
    uint8_t m = (uint8_t)((data >> 8) & 0xFFU);
    uint8_t e = (uint8_t)((data >> 16) & 0xFFU);
    uint8_t version = (uint8_t)((data >> 24) & 0xFFU);

    if (version != ALARM_VERSION)
        return 0;

    if (h > 23U || m > 59U || e > 1U)
        return 0;

    *hour = h;
    *minute = m;
    *enabled = e;

    return 1;
}


HAL_StatusTypeDef AlarmFlash_Save(uint8_t hour,
                                  uint8_t minute,
                                  uint8_t enabled)
{
    if (hour > 23U || minute > 59U || enabled > 1U)
        return HAL_ERROR;

    uint32_t data =
        ((uint32_t)hour) |
        ((uint32_t)minute << 8) |
        ((uint32_t)enabled << 16) |
        ((uint32_t)ALARM_VERSION << 24);

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0;

    HAL_StatusTypeDef rc;

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = ALARM_FLASH_SECTOR;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    rc = HAL_FLASHEx_Erase(&erase, &sector_error);

    if (rc == HAL_OK)
    {
        rc = HAL_FLASH_Program(
            FLASH_TYPEPROGRAM_WORD,
            ALARM_FLASH_ADDR,
            ALARM_MAGIC);
    }

    if (rc == HAL_OK)
    {
        rc = HAL_FLASH_Program(
            FLASH_TYPEPROGRAM_WORD,
            ALARM_FLASH_ADDR + 4U,
            data);
    }

    HAL_FLASH_Lock();

    if (rc != HAL_OK)
        return rc;

    /* Verify */
    if (*(const volatile uint32_t *)ALARM_FLASH_ADDR
            != ALARM_MAGIC)
        return HAL_ERROR;

    if (*(const volatile uint32_t *)(ALARM_FLASH_ADDR + 4U)
            != data)
        return HAL_ERROR;

    return HAL_OK;
}
