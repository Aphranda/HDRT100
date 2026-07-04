#include "ff.h"
#include "diskio.h"

#include "sd_card.h"

#define FATFS_PORT_SD_PDRV 0u

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != FATFS_PORT_SD_PDRV) {
        return STA_NOINIT;
    }

    sd_card_info_t info;
    if (sd_card_probe(&info) != SD_CARD_STATUS_OK || !info.present) {
        return STA_NOINIT | STA_NODISK;
    }
    return 0;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != FATFS_PORT_SD_PDRV) {
        return STA_NOINIT;
    }

    sd_card_info_t info;
    sd_card_get_info(&info);
    if (info.status != SD_CARD_STATUS_OK) {
        return STA_NOINIT;
    }
    return info.present ? 0 : (STA_NOINIT | STA_NODISK);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != FATFS_PORT_SD_PDRV || buff == NULL || count == 0u) {
        return RES_PARERR;
    }

    return sd_card_read_blocks((uint32_t)sector, (uint32_t)count, buff) == SD_CARD_STATUS_OK ?
               RES_OK :
               RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != FATFS_PORT_SD_PDRV || buff == NULL || count == 0u) {
        return RES_PARERR;
    }

    return sd_card_write_blocks((uint32_t)sector, (uint32_t)count, buff) == SD_CARD_STATUS_OK ?
               RES_OK :
               RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != FATFS_PORT_SD_PDRV) {
        return RES_PARERR;
    }

    sd_card_info_t info;
    sd_card_get_info(&info);
    if (info.status != SD_CARD_STATUS_OK || !info.present) {
        return RES_NOTRDY;
    }

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        if (buff == NULL) {
            return RES_PARERR;
        }
        *(LBA_t *)buff = (LBA_t)info.block_count;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (buff == NULL) {
            return RES_PARERR;
        }
        *(WORD *)buff = 512u;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (buff == NULL) {
            return RES_PARERR;
        }
        *(DWORD *)buff = 1u;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
