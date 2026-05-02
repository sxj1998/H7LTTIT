#ifndef USBD_RNDIS_IF_H
#define USBD_RNDIS_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_cdc_rndis.h"

#define CDC_RNDIS_MAC_ADDR0 0x02U
#define CDC_RNDIS_MAC_ADDR1 0x12U
#define CDC_RNDIS_MAC_ADDR2 0x34U
#define CDC_RNDIS_MAC_ADDR3 0x56U
#define CDC_RNDIS_MAC_ADDR4 0x78U
#define CDC_RNDIS_MAC_ADDR5 0x9AU
#define CDC_RNDIS_MAC_STR_DESC "02123456789A"
#define CDC_RNDIS_CONNECT_SPEED_UPSTREAM   12000000U
#define CDC_RNDIS_CONNECT_SPEED_DOWNSTREAM 12000000U
#define USBD_CDC_RNDIS_VID                 0x0483U
#define USBD_CDC_RNDIS_VENDOR_DESC         "STMicroelectronics"
#define USBD_CDC_RNDIS_LINK_SPEED          120000U

extern USBD_CDC_RNDIS_ItfTypeDef USBD_RNDIS_fops_FS;

#ifdef __cplusplus
}
#endif

#endif /* USBD_RNDIS_IF_H */
