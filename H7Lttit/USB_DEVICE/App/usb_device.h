#ifndef __USB_DEVICE_H__
#define __USB_DEVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_def.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

void MX_USB_DEVICE_Init(void);

#ifdef __cplusplus
}
#endif

#endif
