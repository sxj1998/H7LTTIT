#include "littlefs_flash.h"
#include "w25qxx_qspi.h"

#include <string.h>

static lfs_t s_lfs;

static int lfs_flash_read(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          void *buffer,
                          lfs_size_t size);
static int lfs_flash_prog(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          const void *buffer,
                          lfs_size_t size);
static int lfs_flash_erase(const struct lfs_config *c, lfs_block_t block);
static int lfs_flash_sync(const struct lfs_config *c);

static const struct lfs_config s_lfs_cfg = {
    .context = NULL,
    .read = lfs_flash_read,
    .prog = lfs_flash_prog,
    .erase = lfs_flash_erase,
    .sync = lfs_flash_sync,
    .read_size = 16,
    .prog_size = 256,
    .block_size = LITTLEFS_FLASH_BLOCK_SIZE,
    .block_count = LITTLEFS_FLASH_BLOCK_COUNT,
    .block_cycles = 500,
    .cache_size = 256,
    .lookahead_size = 32,
};

int LittleFs_FlashMount(uint8_t format_on_fail)
{
  int err = lfs_mount(&s_lfs, &s_lfs_cfg);

  if ((err != LFS_ERR_OK) && (format_on_fail != 0U))
  {
    err = lfs_format(&s_lfs, &s_lfs_cfg);
    if (err == LFS_ERR_OK)
    {
      err = lfs_mount(&s_lfs, &s_lfs_cfg);
    }
  }

  return err;
}

int LittleFs_FlashUnmount(void)
{
  return lfs_unmount(&s_lfs);
}

lfs_t *LittleFs_FlashGetHandle(void)
{
  return &s_lfs;
}

static int lfs_flash_read(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          void *buffer,
                          lfs_size_t size)
{
  uint32_t address;

  (void)c;
  address = LITTLEFS_FLASH_BASE_ADDR + ((uint32_t)block * LITTLEFS_FLASH_BLOCK_SIZE) + off;
  return (W25qxx_Read((uint8_t *)buffer, address, size) == w25qxx_OK) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int lfs_flash_prog(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          const void *buffer,
                          lfs_size_t size)
{
  uint32_t address;

  (void)c;
  address = LITTLEFS_FLASH_BASE_ADDR + ((uint32_t)block * LITTLEFS_FLASH_BLOCK_SIZE) + off;
  W25qxx_Write((uint8_t *)buffer, address, (uint16_t)size);
  return LFS_ERR_OK;
}

static int lfs_flash_erase(const struct lfs_config *c, lfs_block_t block)
{
  uint32_t address;

  (void)c;
  address = LITTLEFS_FLASH_BASE_ADDR + ((uint32_t)block * LITTLEFS_FLASH_BLOCK_SIZE);
  return (W25qxx_EraseSector(address) == w25qxx_OK) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int lfs_flash_sync(const struct lfs_config *c)
{
  (void)c;
  return LFS_ERR_OK;
}
