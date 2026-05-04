#include "littlefs_sd.h"
#include "sdmmc.h"

static lfs_t s_sd_lfs;
static uint32_t s_sd_block_count = LITTLEFS_SD_BLOCK_COUNT;

static int lfs_sd_read(const struct lfs_config *c,
                       lfs_block_t block,
                       lfs_off_t off,
                       void *buffer,
                       lfs_size_t size);
static int lfs_sd_prog(const struct lfs_config *c,
                       lfs_block_t block,
                       lfs_off_t off,
                       const void *buffer,
                       lfs_size_t size);
static int lfs_sd_erase(const struct lfs_config *c, lfs_block_t block);
static int lfs_sd_sync(const struct lfs_config *c);
static int lfs_sd_wait_ready(void);

static struct lfs_config s_sd_lfs_cfg = {
    .context = NULL,
    .read = lfs_sd_read,
    .prog = lfs_sd_prog,
    .erase = lfs_sd_erase,
    .sync = lfs_sd_sync,
    .read_size = LITTLEFS_SD_BLOCK_SIZE,
    .prog_size = LITTLEFS_SD_BLOCK_SIZE,
    .block_size = LITTLEFS_SD_BLOCK_SIZE,
    .block_count = LITTLEFS_SD_BLOCK_COUNT,
    .block_cycles = 500,
    .cache_size = LITTLEFS_SD_BLOCK_SIZE,
    .lookahead_size = 32,
};

int LittleFs_SdMount(uint8_t format_on_fail)
{
  HAL_SD_CardInfoTypeDef card_info;
  int err;

  if (g_sdmmc1_init_ok == 0U)
  {
    MX_SDMMC1_SD_Init();
  }

  if ((g_sdmmc1_init_ok == 0U) || (HAL_SD_GetCardInfo(&hsd1, &card_info) != HAL_OK))
  {
    return LFS_ERR_IO;
  }

  if (card_info.BlockNbr <= LITTLEFS_SD_START_BLOCK)
  {
    return LFS_ERR_NOSPC;
  }

  s_sd_block_count = card_info.BlockNbr - LITTLEFS_SD_START_BLOCK;
  if (s_sd_block_count > LITTLEFS_SD_BLOCK_COUNT)
  {
    s_sd_block_count = LITTLEFS_SD_BLOCK_COUNT;
  }
  s_sd_lfs_cfg.block_count = s_sd_block_count;

  err = lfs_mount(&s_sd_lfs, &s_sd_lfs_cfg);
  if ((err != LFS_ERR_OK) && (format_on_fail != 0U))
  {
    err = lfs_format(&s_sd_lfs, &s_sd_lfs_cfg);
    if (err == LFS_ERR_OK)
    {
      err = lfs_mount(&s_sd_lfs, &s_sd_lfs_cfg);
    }
  }

  return err;
}

int LittleFs_SdUnmount(void)
{
  return lfs_unmount(&s_sd_lfs);
}

lfs_t *LittleFs_SdGetHandle(void)
{
  return &s_sd_lfs;
}

uint32_t LittleFs_SdGetBlockCount(void)
{
  return s_sd_block_count;
}

static int lfs_sd_read(const struct lfs_config *c,
                       lfs_block_t block,
                       lfs_off_t off,
                       void *buffer,
                       lfs_size_t size)
{
  uint32_t lba;
  uint32_t blocks;

  (void)c;
  if (((off % LITTLEFS_SD_BLOCK_SIZE) != 0U) || ((size % LITTLEFS_SD_BLOCK_SIZE) != 0U))
  {
    return LFS_ERR_INVAL;
  }

  lba = LITTLEFS_SD_START_BLOCK + block + (off / LITTLEFS_SD_BLOCK_SIZE);
  blocks = size / LITTLEFS_SD_BLOCK_SIZE;
  if (HAL_SD_ReadBlocks(&hsd1, (uint8_t *)buffer, lba, blocks, 2000U) != HAL_OK)
  {
    return LFS_ERR_IO;
  }

  return lfs_sd_wait_ready();
}

static int lfs_sd_prog(const struct lfs_config *c,
                       lfs_block_t block,
                       lfs_off_t off,
                       const void *buffer,
                       lfs_size_t size)
{
  uint32_t lba;
  uint32_t blocks;

  (void)c;
  if (((off % LITTLEFS_SD_BLOCK_SIZE) != 0U) || ((size % LITTLEFS_SD_BLOCK_SIZE) != 0U))
  {
    return LFS_ERR_INVAL;
  }

  lba = LITTLEFS_SD_START_BLOCK + block + (off / LITTLEFS_SD_BLOCK_SIZE);
  blocks = size / LITTLEFS_SD_BLOCK_SIZE;
  if (HAL_SD_WriteBlocks(&hsd1, (uint8_t *)buffer, lba, blocks, 5000U) != HAL_OK)
  {
    return LFS_ERR_IO;
  }

  return lfs_sd_wait_ready();
}

static int lfs_sd_erase(const struct lfs_config *c, lfs_block_t block)
{
  (void)c;
  (void)block;
  return LFS_ERR_OK;
}

static int lfs_sd_sync(const struct lfs_config *c)
{
  (void)c;
  return lfs_sd_wait_ready();
}

static int lfs_sd_wait_ready(void)
{
  uint32_t start = HAL_GetTick();

  while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
  {
    if ((HAL_GetTick() - start) > 5000U)
    {
      return LFS_ERR_IO;
    }
  }

  return LFS_ERR_OK;
}
