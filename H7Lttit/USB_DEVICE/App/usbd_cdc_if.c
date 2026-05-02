#include "usbd_cdc_if.h"
#include "usb_device.h"

#include <string.h>

#define APP_RX_DATA_SIZE  2048U
#define APP_TX_DATA_SIZE  2048U

static uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
static uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

static USBD_CDC_LineCodingTypeDef LineCoding = {
  115200U,
  0x00U,
  0x00U,
  0x08U
};

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t *pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS,
  CDC_TransmitCplt_FS
};

static int8_t CDC_Init_FS(void)
{
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0U);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  return (USBD_OK);
}

static int8_t CDC_DeInit_FS(void)
{
  return (USBD_OK);
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
  (void)length;

  switch (cmd)
  {
    case CDC_SET_LINE_CODING:
      LineCoding.bitrate = (uint32_t)(pbuf[0] | (pbuf[1] << 8) |
                                      (pbuf[2] << 16) | (pbuf[3] << 24));
      LineCoding.format = pbuf[4];
      LineCoding.paritytype = pbuf[5];
      LineCoding.datatype = pbuf[6];
      break;

    case CDC_GET_LINE_CODING:
      pbuf[0] = (uint8_t)(LineCoding.bitrate);
      pbuf[1] = (uint8_t)(LineCoding.bitrate >> 8);
      pbuf[2] = (uint8_t)(LineCoding.bitrate >> 16);
      pbuf[3] = (uint8_t)(LineCoding.bitrate >> 24);
      pbuf[4] = LineCoding.format;
      pbuf[5] = LineCoding.paritytype;
      pbuf[6] = LineCoding.datatype;
      break;

    default:
      break;
  }

  return (USBD_OK);
}

static int8_t CDC_Receive_FS(uint8_t *pbuf, uint32_t *Len)
{
  if ((pbuf != NULL) && (Len != NULL) && (*Len > 0U))
  {
    uint32_t tx_len = *Len;
    if (tx_len > APP_TX_DATA_SIZE)
    {
      tx_len = APP_TX_DATA_SIZE;
    }
    memcpy(UserTxBufferFS, pbuf, tx_len);
    (void)CDC_Transmit_FS(UserTxBufferFS, (uint16_t)tx_len);
  }

  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
}

static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum)
{
  (void)pbuf;
  (void)Len;
  (void)epnum;
  return (USBD_OK);
}

uint8_t CDC_IsReady_FS(void)
{
  USBD_CDC_HandleTypeDef *hcdc;

  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
  {
    return 0U;
  }

  hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
  return (hcdc != NULL && hcdc->TxState == 0U) ? 1U : 0U;
}

uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len)
{
  if ((Buf == NULL) || (Len == 0U) || (CDC_IsReady_FS() == 0U))
  {
    return USBD_BUSY;
  }

  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
  return USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}
