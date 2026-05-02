#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS                        1
#define SYS_LIGHTWEIGHT_PROT          0

#define LWIP_IPV4                     1
#define LWIP_IPV6                     0
#define LWIP_ARP                      1
#define LWIP_ETHERNET                 1
#define LWIP_ICMP                     1
#define LWIP_UDP                      1
#define LWIP_TCP                      0
#define LWIP_DHCP                     0
#define LWIP_AUTOIP                   0
#define LWIP_DNS                      0
#define LWIP_NETCONN                  0
#define LWIP_SOCKET                   0
#define LWIP_STATS                    0
#define LWIP_NETIF_HOSTNAME           1
#define LWIP_NETIF_STATUS_CALLBACK    1
#define LWIP_NETIF_LINK_CALLBACK      1
#define LWIP_BROADCAST_PING           1
#define LWIP_MULTICAST_PING           1

#define MEM_ALIGNMENT                 4
#define MEM_SIZE                      (16 * 1024)
#define MEMP_NUM_PBUF                 16
#define MEMP_NUM_UDP_PCB              4
#define MEMP_NUM_SYS_TIMEOUT          6
#define PBUF_POOL_SIZE                16
#define PBUF_POOL_BUFSIZE             1600
#define ETH_PAD_SIZE                  0

#define CHECKSUM_GEN_IP               1
#define CHECKSUM_GEN_UDP              1
#define CHECKSUM_GEN_ICMP             1
#define CHECKSUM_CHECK_IP             1
#define CHECKSUM_CHECK_UDP            1
#define CHECKSUM_CHECK_ICMP           1

#endif /* LWIPOPTS_H */
