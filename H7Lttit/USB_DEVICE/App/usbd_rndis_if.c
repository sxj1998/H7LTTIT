#include "usbd_rndis_if.h"

#include "usb_device.h"
#include "usb_rndis_lwip.h"

static int8_t RNDIS_Itf_Init(void);
static int8_t RNDIS_Itf_DeInit(void);
static int8_t RNDIS_Itf_Control(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t RNDIS_Itf_Receive(uint8_t *buf, uint32_t *len);
static int8_t RNDIS_Itf_TransmitCplt(uint8_t *buf, uint32_t *len, uint8_t epnum);
static int8_t RNDIS_Itf_Process(USBD_HandleTypeDef *pdev);

static uint8_t g_user_rx_buffer[CDC_RNDIS_ETH_FRAME_SIZE_MAX +
                                sizeof(USBD_CDC_RNDIS_PacketMsgTypeDef)] __attribute__((aligned(4)));
static uint8_t g_user_tx_buffer[CDC_RNDIS_ETH_FRAME_SIZE_MAX +
                                sizeof(USBD_CDC_RNDIS_PacketMsgTypeDef)] __attribute__((aligned(4)));

extern USBD_HandleTypeDef hUsbDeviceFS;

USBD_CDC_RNDIS_ItfTypeDef USBD_RNDIS_fops_FS =
{
  RNDIS_Itf_Init,
  RNDIS_Itf_DeInit,
  RNDIS_Itf_Control,
  RNDIS_Itf_Receive,
  RNDIS_Itf_TransmitCplt,
  RNDIS_Itf_Process,
  (uint8_t *)CDC_RNDIS_MAC_STR_DESC,
};

static int8_t RNDIS_Itf_Init(void)
{
  USB_RNDIS_LWIP_Init();
  (void)USBD_CDC_RNDIS_SetTxBuffer(&hUsbDeviceFS, g_user_tx_buffer, 0U);
  (void)USBD_CDC_RNDIS_SetRxBuffer(&hUsbDeviceFS, g_user_rx_buffer);

  return 0;
}

static int8_t RNDIS_Itf_DeInit(void)
{
  USB_RNDIS_LWIP_SetLinkUp(0U);
  return 0;
}

static int8_t RNDIS_Itf_Control(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
  USBD_CDC_RNDIS_HandleTypeDef *hcdc = (USBD_CDC_RNDIS_HandleTypeDef *)hUsbDeviceFS.pClassData;

  (void)pbuf;
  (void)length;

  if ((cmd == CDC_RNDIS_GET_ENCAPSULATED_RESPONSE) && (hcdc != NULL) && (hcdc->LinkStatus == 0U))
  {
    hcdc->LinkStatus = 1U;
    USB_RNDIS_LWIP_SetLinkUp(1U);
  }

  return 0;
}

static int8_t RNDIS_Itf_Receive(uint8_t *buf, uint32_t *len)
{
  USBD_CDC_RNDIS_HandleTypeDef *hcdc = (USBD_CDC_RNDIS_HandleTypeDef *)hUsbDeviceFS.pClassData;

  if ((buf != NULL) && (len != NULL))
  {
    USB_RNDIS_LWIP_Input(buf, *len);
  }

  if (hcdc != NULL)
  {
    hcdc->RxBuffer = g_user_rx_buffer;
    hcdc->RxLength = 0U;
    hcdc->RxState = 0U;
  }

  (void)USBD_CDC_RNDIS_SetRxBuffer(&hUsbDeviceFS, g_user_rx_buffer);
  (void)USBD_CDC_RNDIS_ReceivePacket(&hUsbDeviceFS);

  return 0;
}

static int8_t RNDIS_Itf_TransmitCplt(uint8_t *buf, uint32_t *len, uint8_t epnum)
{
  (void)buf;
  (void)len;
  (void)epnum;

  return 0;
}

static int8_t RNDIS_Itf_Process(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  return 0;
}
