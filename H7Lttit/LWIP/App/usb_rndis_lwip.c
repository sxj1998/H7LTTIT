#include "usb_rndis_lwip.h"

#include "main.h"
#include "usb_device.h"
#include "usbd_cdc_rndis.h"

#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"
#include "netif/ethernet.h"

#include <string.h>

#define RNDIS_MTU              1500U
#define RNDIS_TX_BUFFER_SIZE   (CDC_RNDIS_ETH_FRAME_SIZE_MAX + sizeof(USBD_CDC_RNDIS_PacketMsgTypeDef))

static err_t RNDIS_NetifInit(struct netif *netif);
static err_t RNDIS_LinkOutput(struct netif *netif, struct pbuf *p);

static struct netif g_rndis_netif;
static uint8_t g_lwip_initialized;
static uint8_t g_rndis_tx_buffer[RNDIS_TX_BUFFER_SIZE] __attribute__((aligned(4)));
static const uint8_t g_rndis_mac[6] = {0x02U, 0x12U, 0x34U, 0x56U, 0x78U, 0x9AU};
static volatile uint32_t g_rx_count;
static volatile uint32_t g_tx_count;

extern USBD_HandleTypeDef hUsbDeviceFS;

void USB_RNDIS_LWIP_Init(void)
{
  ip4_addr_t ipaddr;
  ip4_addr_t netmask;
  ip4_addr_t gateway;

  if (g_lwip_initialized != 0U)
  {
    return;
  }

  lwip_init();

  IP4_ADDR(&ipaddr, 192, 168, 7, 1);
  IP4_ADDR(&netmask, 255, 255, 255, 0);
  IP4_ADDR(&gateway, 192, 168, 7, 1);

  (void)netif_add(&g_rndis_netif, &ipaddr, &netmask, &gateway, NULL, RNDIS_NetifInit, ethernet_input);
  netif_set_default(&g_rndis_netif);
  netif_set_up(&g_rndis_netif);
  netif_set_link_down(&g_rndis_netif);

  g_lwip_initialized = 1U;
}

void USB_RNDIS_LWIP_Poll(void)
{
  if (g_lwip_initialized != 0U)
  {
    sys_check_timeouts();
  }
}

void USB_RNDIS_LWIP_SetLinkUp(uint8_t up)
{
  if (g_lwip_initialized == 0U)
  {
    return;
  }

  if (up != 0U)
  {
    netif_set_link_up(&g_rndis_netif);
  }
  else
  {
    netif_set_link_down(&g_rndis_netif);
  }
}

void USB_RNDIS_LWIP_Input(uint8_t *buf, uint32_t len)
{
  struct pbuf *p;

  if ((g_lwip_initialized == 0U) || (buf == NULL) || (len == 0U) || (len > CDC_RNDIS_ETH_FRAME_SIZE_MAX))
  {
    return;
  }

  p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
  if (p == NULL)
  {
    return;
  }

  if (pbuf_take(p, buf, (u16_t)len) == ERR_OK)
  {
    if (g_rndis_netif.input(p, &g_rndis_netif) == ERR_OK)
    {
      g_rx_count++;
      return;
    }
  }

  pbuf_free(p);
}

uint32_t USB_RNDIS_LWIP_GetRxCount(void)
{
  return g_rx_count;
}

uint32_t USB_RNDIS_LWIP_GetTxCount(void)
{
  return g_tx_count;
}

u32_t sys_now(void)
{
  return HAL_GetTick();
}

static err_t RNDIS_NetifInit(struct netif *netif)
{
  netif->name[0] = 'r';
  netif->name[1] = 'n';
  netif->hostname = "h7lttit";
  netif->output = etharp_output;
  netif->linkoutput = RNDIS_LinkOutput;
  netif->mtu = RNDIS_MTU;
  netif->hwaddr_len = ETH_HWADDR_LEN;
  memcpy(netif->hwaddr, g_rndis_mac, sizeof(g_rndis_mac));
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

  return ERR_OK;
}

static err_t RNDIS_LinkOutput(struct netif *netif, struct pbuf *p)
{
  uint16_t frame_len;
  uint8_t *frame;

  (void)netif;

  if ((hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) ||
      (p == NULL) ||
      (p->tot_len > CDC_RNDIS_ETH_FRAME_SIZE_MAX))
  {
    return ERR_IF;
  }

  frame_len = p->tot_len;
  frame = &g_rndis_tx_buffer[sizeof(USBD_CDC_RNDIS_PacketMsgTypeDef)];
  (void)pbuf_copy_partial(p, frame, frame_len, 0);

  if (USBD_CDC_RNDIS_SetTxBuffer(&hUsbDeviceFS, g_rndis_tx_buffer,
                                 (uint32_t)frame_len + sizeof(USBD_CDC_RNDIS_PacketMsgTypeDef)) != USBD_OK)
  {
    return ERR_IF;
  }

  if (USBD_CDC_RNDIS_TransmitPacket(&hUsbDeviceFS) != USBD_OK)
  {
    return ERR_IF;
  }

  g_tx_count++;
  return ERR_OK;
}
