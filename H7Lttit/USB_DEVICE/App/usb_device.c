#include "usb_device.h"
#include "main.h"
#include "usbd_cdc_rndis.h"
#include "usbd_desc.h"
#include "usbd_rndis_if.h"

USBD_HandleTypeDef hUsbDeviceFS;

void MX_USB_DEVICE_Init(void)
{
  if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_RegisterClass(&hUsbDeviceFS, USBD_CDC_RNDIS_CLASS) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_CDC_RNDIS_RegisterInterface(&hUsbDeviceFS, &USBD_RNDIS_fops_FS) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
  {
    Error_Handler();
  }
}
