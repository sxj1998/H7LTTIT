#ifndef W25Q_LAYOUT_H
#define W25Q_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#define W25Q_FLASH_BASE_ADDR (0x00000000UL)
#define W25Q_FLASH_SIZE      (0x01000000UL)
#define W25Q_CODE_BASE_ADDR  W25Q_FLASH_BASE_ADDR
#define W25Q_CODE_SIZE       W25Q_FLASH_SIZE

#ifdef __cplusplus
}
#endif

#endif
