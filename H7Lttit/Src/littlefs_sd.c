#include "littlefs_sd.h"
#include "sdmmc.h"

#include <stdio.h>
#include <string.h>

static lfs_t s_sd_lfs;
static uint32_t s_sd_block_count;
static int s_sd_last_error = LFS_ERR_OK;
static uint32_t s_sd_last_hal_error;
ALIGN_32BYTES(static uint8_t s_sd_original_block[LITTLEFS_SD_BLOCK_SIZE]);
ALIGN_32BYTES(static uint8_t s_sd_pattern_block[LITTLEFS_SD_BLOCK_SIZE]);
ALIGN_32BYTES(static uint8_t s_sd_verify_block[LITTLEFS_SD_BLOCK_SIZE]);

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
static int lfs_sd_ensure_ready(void);
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
    .block_count = 0,
    .block_cycles = 500,
    .cache_size = LITTLEFS_SD_BLOCK_SIZE,
    .lookahead_size = 32,
};

int LittleFs_SdMount(uint8_t format_on_fail)
{
  HAL_SD_CardInfoTypeDef card_info;
  int err;

  s_sd_last_error = LFS_ERR_OK;
  s_sd_last_hal_error = HAL_SD_ERROR_NONE;

  if ((lfs_sd_ensure_ready() != LFS_ERR_OK) ||
      (HAL_SD_GetCardInfo(&hsd1, &card_info) != HAL_OK))
  {
    s_sd_last_error = LFS_ERR_IO;
    s_sd_last_hal_error = hsd1.ErrorCode;
    printf("sd card info failed init=%u state=%lu hal=0x%08lX\r\n",
           g_sdmmc1_init_ok,
           (unsigned long)HAL_SD_GetCardState(&hsd1),
           (unsigned long)s_sd_last_hal_error);
    return LFS_ERR_IO;
  }

  if (card_info.BlockNbr <= LITTLEFS_SD_START_BLOCK)
  {
    s_sd_last_error = LFS_ERR_NOSPC;
    return LFS_ERR_NOSPC;
  }

  s_sd_block_count = card_info.BlockNbr - LITTLEFS_SD_START_BLOCK;
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

  s_sd_last_error = err;
  s_sd_last_hal_error = hsd1.ErrorCode;
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

int LittleFs_SdRawSelfTest(void)
{
  HAL_SD_CardInfoTypeDef card_info;
  uint32_t lba;
  int err;

  s_sd_last_error = LFS_ERR_OK;
  s_sd_last_hal_error = HAL_SD_ERROR_NONE;

  err = lfs_sd_ensure_ready();
  if (err != LFS_ERR_OK)
  {
    return err;
  }

  if (HAL_SD_GetCardInfo(&hsd1, &card_info) != HAL_OK)
  {
    s_sd_last_error = LFS_ERR_IO;
    s_sd_last_hal_error = hsd1.ErrorCode;
    printf("sd raw card info failed state=%lu hal=0x%08lX\r\n",
           (unsigned long)HAL_SD_GetCardState(&hsd1),
           (unsigned long)s_sd_last_hal_error);
    return LFS_ERR_IO;
  }

  if (card_info.BlockNbr <= LITTLEFS_SD_START_BLOCK)
  {
    s_sd_last_error = LFS_ERR_NOSPC;
    printf("sd raw card too small blocks=%lu start=%lu\r\n",
           (unsigned long)card_info.BlockNbr,
           (unsigned long)LITTLEFS_SD_START_BLOCK);
    return LFS_ERR_NOSPC;
  }

  lba = LITTLEFS_SD_START_BLOCK;
  printf("sd raw test card blocks=%lu block_size=%lu lba=%lu clkdiv=%lu bus=%lu\r\n",
         (unsigned long)card_info.BlockNbr,
         (unsigned long)card_info.BlockSize,
         (unsigned long)lba,
         (unsigned long)hsd1.Init.ClockDiv,
         (unsigned long)hsd1.Init.BusWide);

  if (HAL_SD_ReadBlocks(&hsd1, s_sd_original_block, lba, 1U, 2000U) != HAL_OK)
  {
    s_sd_last_error = LFS_ERR_IO;
    s_sd_last_hal_error = hsd1.ErrorCode;
    printf("sd raw read original failed state=%lu hal=0x%08lX\r\n",
           (unsigned long)HAL_SD_GetCardState(&hsd1),
           (unsigned long)s_sd_last_hal_error);
    return LFS_ERR_IO;
  }
  err = lfs_sd_wait_ready();
  if (err != LFS_ERR_OK)
  {
    s_sd_last_error = err;
    return err;
  }

  for (uint32_t i = 0U; i < LITTLEFS_SD_BLOCK_SIZE; i++)
  {
    s_sd_pattern_block[i] = (uint8_t)(0xA5U ^ (uint8_t)i);
  }

  if (HAL_SD_WriteBlocks(&hsd1, s_sd_pattern_block, lba, 1U, 5000U) != HAL_OK)
  {
    s_sd_last_error = LFS_ERR_IO;
    s_sd_last_hal_error = hsd1.ErrorCode;
    printf("sd raw write pattern failed state=%lu hal=0x%08lX\r\n",
           (unsigned long)HAL_SD_GetCardState(&hsd1),
           (unsigned long)s_sd_last_hal_error);
    return LFS_ERR_IO;
  }
  err = lfs_sd_wait_ready();
  if (err != LFS_ERR_OK)
  {
    s_sd_last_error = err;
    return err;
  }

  memset(s_sd_verify_block, 0, sizeof(s_sd_verify_block));
  if (HAL_SD_ReadBlocks(&hsd1, s_sd_verify_block, lba, 1U, 2000U) != HAL_OK)
  {
    s_sd_last_error = LFS_ERR_IO;
    s_sd_last_hal_error = hsd1.ErrorCode;
    printf("sd raw read verify failed state=%lu hal=0x%08lX\r\n",
           (unsigned long)HAL_SD_GetCardState(&hsd1),
           (unsigned long)s_sd_last_hal_error);
    return LFS_ERR_IO;
  }
  err = lfs_sd_wait_ready();
  if (err != LFS_ERR_OK)
  {
    s_sd_last_error = err;
    return err;
  }

  if (memcmp(s_sd_verify_block, s_sd_pattern_block, LITTLEFS_SD_BLOCK_SIZE) != 0)
  {
    s_sd_last_error = LFS_ERR_CORRUPT;
    printf("sd raw verify mismatch first=%02X/%02X last=%02X/%02X\r\n",
           s_sd_verify_block[0],
           s_sd_pattern_block[0],
           s_sd_verify_block[LITTLEFS_SD_BLOCK_SIZE - 1U],
           s_sd_pattern_block[LITTLEFS_SD_BLOCK_SIZE - 1U]);
    return LFS_ERR_CORRUPT;
  }

  if (HAL_SD_WriteBlocks(&hsd1, s_sd_original_block, lba, 1U, 5000U) != HAL_OK)
  {
    s_sd_last_error = LFS_ERR_IO;
    s_sd_last_hal_error = hsd1.ErrorCode;
    printf("sd raw restore failed state=%lu hal=0x%08lX\r\n",
           (unsigned long)HAL_SD_GetCardState(&hsd1),
           (unsigned long)s_sd_last_hal_error);
    return LFS_ERR_IO;
  }
  err = lfs_sd_wait_ready();
  if (err != LFS_ERR_OK)
  {
    s_sd_last_error = err;
    return err;
  }

  printf("sd raw test ok\r\n");
  return LFS_ERR_OK;
}

int LittleFs_SdGetLastError(void)
{
  return s_sd_last_error;
}

uint32_t LittleFs_SdGetLastHalError(void)
{
  return s_sd_last_hal_error;
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
    s_sd_last_hal_error = hsd1.ErrorCode;
    printf("sd read fail block=%lu off=%lu size=%lu lba=%lu blocks=%lu state=%lu hal=0x%08lX\r\n",
           (unsigned long)block,
           (unsigned long)off,
           (unsigned long)size,
           (unsigned long)lba,
           (unsigned long)blocks,
           (unsigned long)HAL_SD_GetCardState(&hsd1),
           (unsigned long)s_sd_last_hal_error);
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
    s_sd_last_hal_error = hsd1.ErrorCode;
    printf("sd write fail block=%lu off=%lu size=%lu lba=%lu blocks=%lu state=%lu hal=0x%08lX\r\n",
           (unsigned long)block,
           (unsigned long)off,
           (unsigned long)size,
           (unsigned long)lba,
           (unsigned long)blocks,
           (unsigned long)HAL_SD_GetCardState(&hsd1),
           (unsigned long)s_sd_last_hal_error);
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

static int lfs_sd_ensure_ready(void)
{
  if (g_sdmmc1_init_ok == 0U)
  {
    MX_SDMMC1_SD_Init();
  }

  if (g_sdmmc1_init_ok == 0U)
  {
    s_sd_last_error = LFS_ERR_IO;
    s_sd_last_hal_error = hsd1.ErrorCode;
    printf("sd init failed hal=0x%08lX\r\n",
           (unsigned long)s_sd_last_hal_error);
    return LFS_ERR_IO;
  }

  return lfs_sd_wait_ready();
}

static int lfs_sd_wait_ready(void)
{
  uint32_t start = HAL_GetTick();

  while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
  {
    if ((HAL_GetTick() - start) > 5000U)
    {
      s_sd_last_hal_error = hsd1.ErrorCode;
      printf("sd wait ready timeout state=%lu hal=0x%08lX\r\n",
             (unsigned long)HAL_SD_GetCardState(&hsd1),
             (unsigned long)s_sd_last_hal_error);
      return LFS_ERR_IO;
    }
  }

  return LFS_ERR_OK;
}
