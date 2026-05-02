#ifndef __USBD_CONF_H__
#define __USBD_CONF_H__

#include "stm32h7xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USBD_MAX_NUM_INTERFACES     2U
#define USBD_MAX_NUM_CONFIGURATION  1U
#define USBD_MAX_STR_DESC_SIZ       64U
#define USBD_SELF_POWERED           1U
#define USBD_DEBUG_LEVEL            0U
#define CDC_RNDIS_ETH_MAX_SEGSZE    1536U

#define USBD_malloc         (void *)USBD_static_malloc
#define USBD_free           USBD_static_free
#define USBD_memset         memset
#define USBD_memcpy         memcpy
#define USBD_Delay          HAL_Delay

#if (USBD_DEBUG_LEVEL > 0U)
#define USBD_UsrLog(...)    do { printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBD_UsrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 1U)
#define USBD_ErrLog(...)    do { printf("ERROR: "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBD_ErrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 2U)
#define USBD_DbgLog(...)    do { printf("DEBUG: "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBD_DbgLog(...)
#endif

void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

#endif
