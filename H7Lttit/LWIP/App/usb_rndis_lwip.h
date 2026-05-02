#ifndef USB_RNDIS_LWIP_H
#define USB_RNDIS_LWIP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void USB_RNDIS_LWIP_Init(void);
void USB_RNDIS_LWIP_Poll(void);
void USB_RNDIS_LWIP_SetLinkUp(uint8_t up);
void USB_RNDIS_LWIP_Input(uint8_t *buf, uint32_t len);
uint32_t USB_RNDIS_LWIP_GetRxCount(void);
uint32_t USB_RNDIS_LWIP_GetTxCount(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_RNDIS_LWIP_H */
