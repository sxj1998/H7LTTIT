#include "usbd_desc.h"
#include "usbd_core.h"

#define USBD_VID                      0x0483U
#define USBD_PID_FS                   0x5741U
#define USBD_LANGID_STRING            0x409U
#define USBD_MANUFACTURER_STRING      "STMicroelectronics"
#define USBD_PRODUCT_STRING_FS        "H7Lttit RNDIS"
#define USBD_CONFIGURATION_STRING_FS  "RNDIS Config"
#define USBD_INTERFACE_STRING_FS      "RNDIS Interface"
#define USB_SIZ_STRING_SERIAL         0x1AU
#define USB_SIZ_BOS_DESC              0x0CU

static void Get_SerialNum(void);
static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len);

static uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ];
static uint8_t USBD_StringSerial[26];

__ALIGN_BEGIN static uint8_t USBD_FS_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
  0x12,
  USB_DESC_TYPE_DEVICE,
  0x00,
  0x02,
  0xE0,
  0x01,
  0x03,
  USB_MAX_EP0_SIZE,
  LOBYTE(USBD_VID),
  HIBYTE(USBD_VID),
  LOBYTE(USBD_PID_FS),
  HIBYTE(USBD_PID_FS),
  0x00,
  0x02,
  USBD_IDX_MFC_STR,
  USBD_IDX_PRODUCT_STR,
  USBD_IDX_SERIAL_STR,
  USBD_MAX_NUM_CONFIGURATION
};

__ALIGN_BEGIN static uint8_t USBD_FS_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
  USB_LEN_LANGID_STR_DESC,
  USB_DESC_TYPE_STRING,
  LOBYTE(USBD_LANGID_STRING),
  HIBYTE(USBD_LANGID_STRING)
};

#if ((USBD_LPM_ENABLED == 1U) || (USBD_CLASS_BOS_ENABLED == 1U))
__ALIGN_BEGIN static uint8_t USBD_FS_BOSDesc[USB_SIZ_BOS_DESC] __ALIGN_END = {
  0x05,
  USB_DESC_TYPE_BOS,
  0x0C,
  0x00,
  0x01,
  0x07,
  USB_DEVICE_CAPABITY_TYPE,
  0x02,
  0x02,
  0x00,
  0x00,
  0x00
};
#endif

static uint8_t *USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  *length = sizeof(USBD_FS_DeviceDesc);
  return USBD_FS_DeviceDesc;
}

static uint8_t *USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  *length = sizeof(USBD_FS_LangIDDesc);
  return USBD_FS_LangIDDesc;
}

static uint8_t *USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

static uint8_t *USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_GetString((uint8_t *)USBD_PRODUCT_STRING_FS, USBD_StrDesc, length);
  return USBD_StrDesc;
}

static uint8_t *USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  *length = USB_SIZ_STRING_SERIAL;
  Get_SerialNum();
  return USBD_StringSerial;
}

static uint8_t *USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING_FS, USBD_StrDesc, length);
  return USBD_StrDesc;
}

static uint8_t *USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_GetString((uint8_t *)USBD_INTERFACE_STRING_FS, USBD_StrDesc, length);
  return USBD_StrDesc;
}

#if ((USBD_LPM_ENABLED == 1U) || (USBD_CLASS_BOS_ENABLED == 1U))
static uint8_t *USBD_FS_USR_BOSDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  *length = sizeof(USBD_FS_BOSDesc);
  return USBD_FS_BOSDesc;
}
#endif

USBD_DescriptorsTypeDef FS_Desc = {
  USBD_FS_DeviceDescriptor,
  USBD_FS_LangIDStrDescriptor,
  USBD_FS_ManufacturerStrDescriptor,
  USBD_FS_ProductStrDescriptor,
  USBD_FS_SerialStrDescriptor,
  USBD_FS_ConfigStrDescriptor,
  USBD_FS_InterfaceStrDescriptor
#if ((USBD_LPM_ENABLED == 1U) || (USBD_CLASS_BOS_ENABLED == 1U))
  ,
  USBD_FS_USR_BOSDescriptor
#endif
};

static void Get_SerialNum(void)
{
  uint32_t deviceserial0 = *(uint32_t *)UID_BASE;
  uint32_t deviceserial1 = *(uint32_t *)(UID_BASE + 4U);
  uint32_t deviceserial2 = *(uint32_t *)(UID_BASE + 8U);

  deviceserial0 += deviceserial2;

  if (deviceserial0 != 0U)
  {
    USBD_StringSerial[0] = USB_SIZ_STRING_SERIAL;
    USBD_StringSerial[1] = USB_DESC_TYPE_STRING;
    IntToUnicode(deviceserial0, &USBD_StringSerial[2], 8U);
    IntToUnicode(deviceserial1, &USBD_StringSerial[18], 4U);
  }
}

static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
  uint8_t idx;

  for (idx = 0U; idx < len; idx++)
  {
    if (((value >> 28)) < 0xAU)
    {
      pbuf[2U * idx] = (uint8_t)((value >> 28) + '0');
    }
    else
    {
      pbuf[2U * idx] = (uint8_t)((value >> 28) + 'A' - 10U);
    }
    value <<= 4;
    pbuf[(2U * idx) + 1U] = 0U;
  }
}
