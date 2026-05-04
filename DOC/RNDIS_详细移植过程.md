# RNDIS 详细移植过程

## 1. 工程基础

- MCU：`STM32H750VBTx`
- 工程：`H7Lttit/MDK-ARM/H7Lttit.uvprojx`
- 工具链：Keil MDK-ARM 5.35，ARMCC 5.06 update 7
- USB：使用 `USB_OTG_FS`
- RNDIS MAC：`02:12:34:56:78:9A`
- 设备端静态 IP：`192.168.7.1/24`
- LwIP 模式：`NO_SYS = 1`，不启用 tcpip 线程

## 2. 添加 USB Device RNDIS 类

在 ST USB Device Library 下增加 RNDIS 类文件：

```text
H7Lttit/Middlewares/ST/STM32_USB_Device_Library/Class/CDC_RNDIS/Inc/usbd_cdc_rndis.h
H7Lttit/Middlewares/ST/STM32_USB_Device_Library/Class/CDC_RNDIS/Src/usbd_cdc_rndis.c
```

Keil 工程中新增分组：

```text
Middlewares/USB_Device/CDC_RNDIS
```

并把 `usbd_cdc_rndis.c` 加入编译。

## 3. 添加 USB_DEVICE 应用层文件

增加或修改以下文件：

```text
H7Lttit/USB_DEVICE/App/usb_device.c
H7Lttit/USB_DEVICE/App/usbd_desc.c
H7Lttit/USB_DEVICE/App/usbd_rndis_if.c
H7Lttit/USB_DEVICE/App/usbd_rndis_if.h
H7Lttit/USB_DEVICE/Target/usbd_conf.c
H7Lttit/USB_DEVICE/Target/usbd_conf.h
```

`usb_device.c` 负责初始化 USB Device：

```c
USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS);
USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC_RNDIS);
USBD_CDC_RNDIS_RegisterInterface(&hUsbDeviceFS, &USBD_RNDIS_fops_FS);
USBD_Start(&hUsbDeviceFS);
```

`usbd_rndis_if.c` 负责 RNDIS 和 LwIP 之间的数据收发衔接：

- RNDIS 初始化时调用 `USB_RNDIS_LWIP_Init()`
- USB 收到以太网帧后调用 `USB_RNDIS_LWIP_Input()`
- 发送完成后更新 RNDIS 发送状态
- 控制请求中处理 link up / link down

## 4. 配置 USB 底层驱动和中断

`usbd_conf.c` 中初始化 `PCD_HandleTypeDef hpcd_USB_OTG_FS`，并设置 USB FS 中断：

```c
HAL_NVIC_SetPriority(OTG_FS_IRQn, 6U, 0U);
HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
```

`stm32h7xx_it.c` 中添加：

```c
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

void OTG_FS_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}
```

`main.c` 初始化 USB 前启用 USB 电压检测：

```c
HAL_PWREx_EnableUSBVoltageDetector();
MX_USB_DEVICE_Init();
```

## 5. 添加 LwIP

添加 LwIP 核心源码和 IPv4/以太网相关文件：

```text
H7Lttit/Middlewares/Third_Party/LwIP/src/core/*.c
H7Lttit/Middlewares/Third_Party/LwIP/src/core/ipv4/*.c
H7Lttit/Middlewares/Third_Party/LwIP/src/netif/ethernet.c
```

添加应用层适配文件：

```text
H7Lttit/LWIP/App/lwipopts.h
H7Lttit/LWIP/App/usb_rndis_lwip.c
H7Lttit/LWIP/App/usb_rndis_lwip.h
```

Keil include path 增加：

```text
../LWIP/App
../Middlewares/Third_Party/LwIP/src/include
../Middlewares/Third_Party/LwIP/system
../Middlewares/Third_Party/LwIP/system/arch
```

## 6. 配置 `lwipopts.h`

当前配置为轻量 raw/no-OS 模式：

```c
#define NO_SYS                        1
#define LWIP_IPV4                     1
#define LWIP_IPV6                     0
#define LWIP_ARP                      1
#define LWIP_ETHERNET                 1
#define LWIP_ICMP                     1
#define LWIP_UDP                      1
#define LWIP_TCP                      0
#define LWIP_DHCP                     0
#define LWIP_DNS                      0
#define LWIP_NETCONN                  0
#define LWIP_SOCKET                   0
```

内存池关键配置：

```c
#define MEM_SIZE                      (16 * 1024)
#define MEMP_NUM_PBUF                 16
#define MEMP_NUM_UDP_PCB              4
#define MEMP_NUM_SYS_TIMEOUT          6
#define PBUF_POOL_SIZE                16
#define PBUF_POOL_BUFSIZE             1600
```

这里的 `MEM_SIZE` 和 `PBUF_POOL_*` 是 RNDIS/LwIP RAM 占用的大头。

## 7. 实现 RNDIS 网络接口

`usb_rndis_lwip.c` 中实现一个 LwIP `netif`：

- `USB_RNDIS_LWIP_Init()`：初始化 LwIP、添加 netif、设置静态 IP
- `USB_RNDIS_LWIP_Input()`：把 USB 收到的以太网帧放入 pbuf，然后交给 LwIP
- `RNDIS_LinkOutput()`：LwIP 发包时封装成 RNDIS 数据包并通过 USB 发出
- `USB_RNDIS_LWIP_Poll()`：调用 `sys_check_timeouts()`

静态网络参数：

```text
IP:      192.168.7.1
Netmask: 255.255.255.0
Gateway: 192.168.7.1
MAC:     02:12:34:56:78:9A
```

## 8. 在主循环或任务中轮询 LwIP

因为当前 `NO_SYS = 1`，没有 LwIP 专用线程，必须周期性调用：

```c
USB_RNDIS_LWIP_Poll();
```

移植 FreeRTOS 后，该调用放在默认任务中，每 10 ms 执行一次。

## 9. 编译和烧录验证

全量编译：

```powershell
& 'C:\Keil_v5\UV4\UV4.exe' -r 'H7Lttit\MDK-ARM\H7Lttit.uvprojx' -j0
```

烧录：

```powershell
& 'C:\Keil_v5\UV4\UV4.exe' -f 'H7Lttit\MDK-ARM\H7Lttit.uvprojx' -j0
```

J-Link 读回校验：

```powershell
& 'C:\Keil_v5\ARM\ARMCC\bin\fromelf.exe' --bin --output='H7Lttit\MDK-ARM\H7Lttit\H7Lttit.bin' 'H7Lttit\MDK-ARM\H7Lttit\H7Lttit.axf'
& 'C:\Keil_v5\ARM\Segger\JLink.exe' -CommanderScript 'jlink_verify.jlink'
```

校验结果：

```text
Reading 61576 bytes data from target memory @ 0x08000000.
Verify successful.
```

## 10. Windows 枚举和 ping 验证

Windows 成功枚举：

```text
InterfaceDescription: Remote NDIS based Internet Sharing Device
Status: Up
MAC: 02-12-34-56-78-9A
LinkSpeed: 12 Mbps
```

ping 设备静态 IP：

```text
ping 192.168.7.1 -n 4
Reply from 192.168.7.1: bytes=32 time=5ms TTL=248
Reply from 192.168.7.1: bytes=32 time=5ms TTL=248
Reply from 192.168.7.1: bytes=32 time=4ms TTL=248
Reply from 192.168.7.1: bytes=32 time=4ms TTL=248
Packets: Sent = 4, Received = 4, Lost = 0 (0% loss)
```

## 11. 注意事项

- `NO_SYS = 1` 时必须持续调用 `sys_check_timeouts()`，否则 ARP、IP 分片等超时逻辑不会运行。
- USB 中断优先级当前为 `6`，配合 FreeRTOS 的 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5` 使用是安全的。
- 如果后续启用 DHCP/TCP/socket，Flash 和 RAM 都会明显增加。
- 当前 Windows 网卡可能拿到 `169.254.x.x` 自动地址，但仍可以访问设备端静态 IP `192.168.7.1`。
