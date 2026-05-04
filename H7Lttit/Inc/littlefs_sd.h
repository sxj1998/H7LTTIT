#ifndef LITTLEFS_SD_H
#define LITTLEFS_SD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "lfs.h"

#define LITTLEFS_SD_START_BLOCK  (32768UL)
#define LITTLEFS_SD_BLOCK_SIZE   (512UL)
#define LITTLEFS_SD_BLOCK_COUNT  (32768UL)
#define LITTLEFS_SD_SIZE         (LITTLEFS_SD_BLOCK_SIZE * LITTLEFS_SD_BLOCK_COUNT)

int LittleFs_SdMount(uint8_t format_on_fail);
int LittleFs_SdUnmount(void);
lfs_t *LittleFs_SdGetHandle(void);
uint32_t LittleFs_SdGetBlockCount(void);

#ifdef __cplusplus
}
#endif

#endif
