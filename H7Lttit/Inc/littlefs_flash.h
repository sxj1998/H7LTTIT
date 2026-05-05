#ifndef LITTLEFS_FLASH_H
#define LITTLEFS_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "lfs.h"
#include "w25q_layout.h"

#define LITTLEFS_FLASH_BASE_ADDR   (W25Q_FLASH_BASE_ADDR + W25Q_FLASH_SIZE)
#define LITTLEFS_FLASH_BLOCK_SIZE  (4096UL)
#define LITTLEFS_FLASH_BLOCK_COUNT (0UL)
#define LITTLEFS_FLASH_SIZE        (LITTLEFS_FLASH_BLOCK_SIZE * LITTLEFS_FLASH_BLOCK_COUNT)

int LittleFs_FlashMount(uint8_t format_on_fail);
int LittleFs_FlashUnmount(void);
lfs_t *LittleFs_FlashGetHandle(void);

#ifdef __cplusplus
}
#endif

#endif
