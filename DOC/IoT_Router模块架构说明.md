# IoT Router 模块架构说明

## 1. 模块定位

IoT Router 是一个轻量级数据路由模块，用来解决 MCU 工程中多输入、多输出、多处理逻辑混在一起的问题。

在没有路由层时，代码通常会变成这种结构：

```text
USART1 收到数据 -> 在 USART1 回调里判断 OTA/日志/命令/转发
USART2 收到数据 -> 再写一套类似判断
USB 收到数据    -> 再写一套类似判断
```

这种写法的问题是：
- 数据来源越多，判断逻辑越分散。
- 特殊命令，例如 OTA、配置命令、日志上传，会散落在各个驱动回调里。
- 换平台时，需要同时改串口、USB、业务逻辑。
- 后续增加“串口转 USB”“USB 转串口”“某类命令单独处理”等需求时，容易互相影响。

IoT Router 的设计目标是把这些问题拆开：

```text
输入来源 source -> 路由规则 rule -> 输出通道 channel / 特殊处理 action
```

当前工程中已经接入：
- `USART1/COM3`：调试 shell，同时旁路进入路由器，来源名为 `uart`。
- `USART2/COM8`：第二路外部串口，来源名为 `uart2`。
- USB：预留来源名为 `usb`，当前工程 USB 类是 RNDIS，未启用 CDC 串口流。
- OTA 检测：任意已接入来源中包含 `ota` 时，进入 OTA action。

## 2. 分层设计

模块分成两层：

```text
可移植核心层 iot_router.c/.h
  只理解 source、channel、rule、action。
  不包含 STM32 HAL、寄存器、FreeRTOS、USB Device 依赖。

平台适配层 iot_router_port.c/.h
  负责把当前板子的 USART1、USART2、USB、debug 输出接到核心层。
  包含 STM32 寄存器访问、缓冲区、队列、默认规则注册。
```

这样做的核心思路是：
- 核心层只描述“数据如何被路由”。
- port 层只描述“数据如何进入和离开当前硬件平台”。
- 业务规则通过结构体配置，不直接写死在串口中断里。

## 3. 文件结构

```text
H7Lttit/iot_router/
├── include/
│   ├── iot_router.h        # 可移植核心接口和结构体定义
│   └── iot_router_port.h   # 当前 STM32 工程的 port 接口
└── source/
    ├── iot_router.c        # 路由核心实现
    └── iot_router_port.c   # USART/USB/debug/OTA 的平台绑定
```

工程接入点：

```text
H7Lttit/Src/main.c
  初始化 USART1、USART2、IoT Router。
  在默认任务中周期调用 IotRouter_PortProcess()。

H7Lttit/Src/stm32h7xx_it.c
  USART2_IRQHandler() 读取 USART2 数据并提交给路由器。

H7Lttit/shell/source/comm.c
  USART1 原本用于 shell。
  当前保留 shell 接收路径，同时旁路一份数据给路由器。

H7Lttit/MDK-ARM/H7Lttit.uvprojx
  增加 iot_router/include。
  增加 iot_router.c 和 iot_router_port.c。
```

## 4. 总体数据流

### 4.1 USART1/COM3

```text
USART1_IRQHandler()
  -> comm_uart_irq_handler()
      -> uart_rx_push(ch)                  # 原 shell 路径
      -> IotRouter_PortSubmitUartByte(ch)  # 路由旁路
```

USART1 的输入不会被路由器拿走，所以 shell 仍然可以正常工作。路由器只是监听一份同样的数据。

### 4.2 USART2/COM8

```text
USART2_IRQHandler()
  -> 读取 USART2->RDR
  -> IotRouter_PortSubmitUart2Byte(ch)
      -> port_submit_line()
      -> port_enqueue_packet()
```

USART2 当前专门作为第二路 IoT 输入源，没有接 shell。

### 4.3 任务中执行路由

中断里不做规则匹配和串口输出，只做轻量缓存和入队：

```text
StartDefaultTask()
  -> IotRouter_PortProcess()
      -> port_dequeue_packet()
      -> IotRouter_RoutePacket()
          -> 匹配规则
          -> 执行 action
          -> 转发到 channel
```

这样设计是为了避免在中断中执行阻塞式串口发送。当前 `port_uart_write()` 会等待 USART1 TX 空，因此必须放在任务上下文中执行。

## 5. 核心抽象

IoT Router 的核心抽象有三个：

```text
source  数据来自哪里
rule    满足什么条件后怎么处理
channel 数据发往哪里
```

这三个概念解耦后，可以组合出不同业务：

```text
uart2 + contains("ota") -> ota action
uart2 + any             -> debug channel
uart  + any             -> usb channel
usb   + any             -> uart channel
app   + event           -> debug channel
```

## 6. source 设计

source 使用 bit mask 表示，一个数据包可以标记一个来源，也可以在需要时表达多个来源组合。

当前定义：

```c
#define IOT_ROUTER_SOURCE_UART  (1UL << 0)
#define IOT_ROUTER_SOURCE_USB   (1UL << 1)
#define IOT_ROUTER_SOURCE_APP   (1UL << 2)
#define IOT_ROUTER_SOURCE_UART2 (1UL << 3)
```

设计思路：
- 使用 bit mask 后，一个规则可以同时匹配多个来源。
- 例如 OTA 规则可以写成 `UART | UART2 | USB`，不需要注册三条规则。
- 后续新增 `UART3`、`CAN`、`BLE`、`ETH` 时，只需要增加新的 bit。

来源名称由 `IotRouter_SourceName()` 转成字符串：

```text
IOT_ROUTER_SOURCE_UART  -> "uart"
IOT_ROUTER_SOURCE_UART2 -> "uart2"
IOT_ROUTER_SOURCE_USB   -> "usb"
IOT_ROUTER_SOURCE_APP   -> "app"
```

这个名称用于 debug 输出中的来源标签：

```text
[uart2] hello
```

## 7. channel 设计

channel 表示数据可以被转发到哪里。

当前定义：

```c
#define IOT_ROUTER_CHANNEL_DEBUG (1UL << 0)
#define IOT_ROUTER_CHANNEL_UART  (1UL << 1)
#define IOT_ROUTER_CHANNEL_USB   (1UL << 2)
#define IOT_ROUTER_CHANNEL_OTA   (1UL << 3)
```

和 source 一样，channel 也使用 bit mask。这样一条规则可以输出到多个通道：

```text
uart2 -> debug + usb
usb   -> debug + uart
ota   -> debug + ota
```

当前工程中：
- `DEBUG` 绑定到 USART1/COM3，用来观察路由结果。
- `UART` 也绑定到 USART1，作为普通串口输出通道预留。
- `USB` 已注册但当前 `port_usb_write()` 返回失败，因为当前 USB 是 RNDIS，不是 CDC。
- `OTA` 当前也绑定到 USART1，用于输出 OTA 检测信息；后续可以改成真正 OTA 状态机入口。

## 8. 结构体详解

### 8.1 `IotRouterStatus`

```c
typedef enum {
    IOT_ROUTER_OK = 0,
    IOT_ROUTER_EINVAL = -1,
    IOT_ROUTER_ENOSPC = -2,
    IOT_ROUTER_EIO = -3,
} IotRouterStatus;
```

含义：
- `IOT_ROUTER_OK`：操作成功。
- `IOT_ROUTER_EINVAL`：参数非法，例如空指针、id 为 0、重复注册。
- `IOT_ROUTER_ENOSPC`：静态数组空间不足，例如 channel 或 rule 数量超过上限。
- `IOT_ROUTER_EIO`：输出通道写入失败。

设计思路：
- 不使用 `errno`，避免依赖 C 运行库环境。
- 返回值固定，便于裸机和 RTOS 工程统一处理。
- 错误码为负数，成功为 0，符合常见嵌入式 C 风格。

### 8.2 `IotRouterWriteFn`

```c
typedef int (*IotRouterWriteFn)(void *ctx, const uint8_t *data, size_t len);
```

这是 channel 的写函数指针。

字段含义：
- `ctx`：通道私有上下文。例如可以传 UART 句柄、USB CDC 句柄、socket 句柄。
- `data`：要发送的数据。
- `len`：数据长度。

返回值约定：
- `>= 0`：写入成功。
- `< 0`：写入失败。

设计思路：
- 核心层不关心 UART、USB、TCP 如何发送，只调用 `write()`。
- 移植到其他平台时，只替换 write 函数即可。
- `ctx` 让同一套 write 函数可以复用到多个实例。

当前示例：

```c
debug_channel.write = port_uart_write;
usb_channel.write = port_usb_write;
```

### 8.3 `IotRouterActionFn`

```c
typedef IotRouterStatus (*IotRouterActionFn)(struct IotRouter *router,
                                             const struct IotRouterRule *rule,
                                             uint32_t source,
                                             const uint8_t *data,
                                             size_t len,
                                             void *ctx);
```

这是规则命中后的业务处理函数。

字段含义：
- `router`：当前路由器实例。action 内可以继续调用 `IotRouter_Forward()` 输出日志或结果。
- `rule`：当前命中的规则，可以读取规则名、输出通道、统计信息等。
- `source`：本次数据来源，例如 `IOT_ROUTER_SOURCE_UART2`。
- `data`：本次数据内容。
- `len`：数据长度。
- `ctx`：action 私有上下文，可传业务状态机、配置表、flash 句柄等。

设计思路：
- 普通转发用 `output_mask` 即可。
- 复杂业务，例如 OTA、AT 指令解析、JSON 配置、加密解密，放进 action。
- action 可选，不需要业务处理时填 `NULL`。

当前 OTA 示例：

```c
ota_rule.action = port_ota_action;
```

### 8.4 `IotRouterChannel`

```c
typedef struct {
    uint32_t id;
    const char *name;
    IotRouterWriteFn write;
    void *ctx;
    uint32_t tx_packets;
    uint32_t tx_bytes;
    uint32_t tx_errors;
} IotRouterChannel;
```

字段说明：

| 字段 | 含义 | 设计原因 |
| --- | --- | --- |
| `id` | 通道 bit，例如 `IOT_ROUTER_CHANNEL_DEBUG` | 用 bit mask 支持一条规则输出到多个通道 |
| `name` | 通道名称 | 便于调试、统计、后续命令行查询 |
| `write` | 通道写函数 | 核心层通过函数指针调用具体平台输出 |
| `ctx` | 通道私有上下文 | 让同一个 write 可绑定不同硬件实例 |
| `tx_packets` | 成功输出包数 | 运行时统计 |
| `tx_bytes` | 成功输出字节数 | 运行时统计 |
| `tx_errors` | 输出失败次数 | 发现 USB 未就绪、串口异常等问题 |

为什么 channel 用结构体，而不是直接在规则里写函数：
- 多条规则可以复用同一个输出通道。
- 输出通道可以集中统计。
- 规则只表达“输出到哪个通道”，不需要知道硬件细节。

### 8.5 `IotRouterRule`

```c
typedef struct IotRouterRule {
    const char *name;
    uint32_t source_mask;
    const char *contains;
    uint32_t output_mask;
    uint32_t flags;
    IotRouterActionFn action;
    void *action_ctx;
    uint32_t match_count;
    uint32_t drop_count;
} IotRouterRule;
```

字段说明：

| 字段 | 含义 | 设计原因 |
| --- | --- | --- |
| `name` | 规则名，例如 `ota_detect` | 便于启用/禁用、调试、统计 |
| `source_mask` | 允许匹配的来源集合 | 一条规则可以匹配多个输入来源 |
| `contains` | 文本匹配关键字，`NULL` 或空字符串表示任意数据 | 满足 OTA/命令关键字等简单场景 |
| `output_mask` | 命中后输出到哪些通道 | 支持一对多转发 |
| `flags` | 规则行为控制 | 控制是否加来源标签、大小写、是否继续匹配 |
| `action` | 命中后的业务处理函数 | 支持 OTA 等复杂逻辑 |
| `action_ctx` | action 私有上下文 | 传递业务对象，保持核心层无业务依赖 |
| `match_count` | 规则命中次数 | 运行时诊断 |
| `drop_count` | 规则处理或转发失败次数 | 运行时诊断 |

规则匹配逻辑：

```text
1. 规则必须启用。
2. source 必须落在 source_mask 中。
3. contains 为空则直接匹配；不为空则在 data 中查找关键字。
4. 匹配后先执行 action。
5. 如果 output_mask 非 0，则执行转发。
6. 如果设置 CONSUME_ON_MATCH，则不再继续匹配后续规则。
```

### 8.6 `IotRouterStats`

```c
typedef struct {
    uint32_t rx_packets;
    uint32_t rx_bytes;
    uint32_t routed_packets;
    uint32_t dropped_packets;
} IotRouterStats;
```

字段说明：

| 字段 | 含义 |
| --- | --- |
| `rx_packets` | 路由器收到的数据包数量 |
| `rx_bytes` | 路由器收到的总字节数 |
| `routed_packets` | 成功转发的数据包数量 |
| `dropped_packets` | 未匹配规则或处理失败的数据包数量 |

设计思路：
- 统计数据放在核心层，不依赖 debug 输出。
- 后续可以通过 shell 命令读取 `IotRouter_GetStats()` 输出运行状态。
- 当前还没有做 shell 命令展示，但接口已经预留。

### 8.7 `IotRouter`

```c
typedef struct IotRouter {
    IotRouterChannel channels[IOT_ROUTER_MAX_CHANNELS];
    IotRouterRule rules[IOT_ROUTER_MAX_RULES];
    IotRouterStats stats;
    uint8_t channel_count;
    uint8_t rule_count;
} IotRouter;
```

字段说明：

| 字段 | 含义 | 设计原因 |
| --- | --- | --- |
| `channels[]` | 已注册通道表 | 静态分配，避免 MCU 动态内存碎片 |
| `rules[]` | 已注册规则表 | 规则数量可配置，查找简单 |
| `stats` | 路由器整体统计 | 诊断数据流是否正常 |
| `channel_count` | 当前通道数量 | 遍历 channels 时使用 |
| `rule_count` | 当前规则数量 | 遍历 rules 时使用 |

为什么不用链表：
- 当前规则数量很少，数组遍历成本低。
- 静态数组更适合 MCU。
- 不需要 malloc/free。
- Keil/裸机/RTOS 都容易移植。

为什么 rule 和 channel 都放在 `IotRouter` 里：
- 允许后续创建多个 router 实例。
- 例如一个 router 处理设备控制数据，另一个 router 处理日志数据。
- 当前工程只使用一个静态实例 `s_router`。

## 9. flag 设计

当前规则 flag：

```c
#define IOT_ROUTER_RULE_TAG_SOURCE       (1UL << 0)
#define IOT_ROUTER_RULE_CONSUME_ON_MATCH (1UL << 1)
#define IOT_ROUTER_RULE_CASE_INSENSITIVE (1UL << 2)
#define IOT_ROUTER_RULE_ENABLED          (1UL << 31)
```

### 9.1 `IOT_ROUTER_RULE_TAG_SOURCE`

输出前增加来源标签：

```text
[uart2] hello
```

适合 debug、日志、混合通道转发。

如果是二进制协议转发，通常不应该打开这个 flag，否则会破坏协议。

### 9.2 `IOT_ROUTER_RULE_CONSUME_ON_MATCH`

命中规则后停止继续匹配后续规则。

OTA 规则当前设置了该 flag：

```c
ota_rule.flags = IOT_ROUTER_RULE_CASE_INSENSITIVE |
                 IOT_ROUTER_RULE_TAG_SOURCE |
                 IOT_ROUTER_RULE_CONSUME_ON_MATCH;
```

设计意图：
- 特殊命令优先处理。
- 避免 OTA 数据继续被普通透传规则转发。

当前实现中，OTA 规则仍然有 `output_mask = IOT_ROUTER_CHANNEL_OTA`，所以会输出 OTA 通道信息，但不会继续走后面的普通规则。

### 9.3 `IOT_ROUTER_RULE_CASE_INSENSITIVE`

`contains` 匹配忽略大小写。

例如以下内容都会命中 OTA：

```text
ota
OTA
Start_Ota
```

### 9.4 `IOT_ROUTER_RULE_ENABLED`

规则启用位。

调用 `IotRouter_AddRule()` 时会自动设置该位。运行时可以通过：

```c
IotRouter_SetRuleEnabled(&s_router, "uart_to_usb", 0U);
IotRouter_SetRuleEnabled(&s_router, "uart_to_usb", 1U);
```

动态关闭或打开规则。

## 10. 核心函数设计

### 10.1 `IotRouter_Init()`

```c
void IotRouter_Init(IotRouter *router);
```

作用：
- 清空 router 全部状态。
- 清空通道表、规则表和统计信息。

调用位置：

```c
IotRouter_Init(&s_router);
```

### 10.2 `IotRouter_AddChannel()`

```c
IotRouterStatus IotRouter_AddChannel(IotRouter *router,
                                     const IotRouterChannel *channel);
```

作用：
- 注册一个输出通道。
- 检查参数是否合法。
- 检查通道 id 是否重复。
- 检查通道数组是否还有空间。

设计原因：
- 通道必须先注册，规则才能输出到对应 channel。
- 注册时集中做合法性检查，减少运行时错误。

### 10.3 `IotRouter_AddRule()`

```c
IotRouterStatus IotRouter_AddRule(IotRouter *router,
                                  const IotRouterRule *rule);
```

作用：
- 注册一条路由规则。
- 检查规则名、来源、重复规则、数组容量。
- 自动设置 `IOT_ROUTER_RULE_ENABLED`。

设计原因：
- 规则可以在 port 初始化阶段集中配置。
- 后续也可以从配置文件或 shell 命令动态生成规则。

### 10.4 `IotRouter_RoutePacket()`

```c
IotRouterStatus IotRouter_RoutePacket(IotRouter *router,
                                      uint32_t source,
                                      const uint8_t *data,
                                      size_t len);
```

这是核心入口。

处理流程：

```text
收到 source + data
  -> 更新 rx 统计
  -> 从第一条规则开始遍历
  -> 判断规则是否启用
  -> 判断 source 是否匹配
  -> 判断 contains 是否匹配
  -> 命中后执行 action
  -> 命中后按 output_mask 转发
  -> 根据 CONSUME_ON_MATCH 决定是否继续
```

设计原因：
- 所有输入来源最终都进入同一个函数。
- 业务扩展集中在规则表中，不散落到 ISR 和驱动层。

### 10.5 `IotRouter_Forward()`

```c
IotRouterStatus IotRouter_Forward(IotRouter *router,
                                  uint32_t output_mask,
                                  uint32_t source,
                                  const uint8_t *data,
                                  size_t len,
                                  uint32_t flags);
```

作用：
- 根据 output mask 查找所有目标 channel。
- 如果设置 `TAG_SOURCE`，先输出 `[source] `。
- 调用每个 channel 的 `write()`。
- 更新 channel 的 tx 统计。

设计原因：
- action 内也可以复用转发能力。
- 普通规则转发和特殊 action 输出使用同一套 channel 机制。

## 11. port 层结构和设计

`iot_router_port.c` 是当前 STM32H750 工程的适配层。它做三件事：

```text
1. 创建 router 实例并注册通道/规则。
2. 接收 USART1、USART2、USB 数据并转换为 packet。
3. 在任务上下文中调用核心路由函数。
```

### 11.1 静态 router 实例

```c
static IotRouter s_router;
static volatile uint8_t s_router_ready;
```

含义：
- `s_router` 是当前工程唯一的路由器实例。
- `s_router_ready` 防止初始化前中断误提交数据。

为什么用静态实例：
- 当前工程只需要一个全局路由器。
- 不需要动态创建对象。
- 中断和任务都能访问同一个实例。

### 11.2 行缓冲

```c
static uint8_t s_uart_line[IOT_ROUTER_LINE_BUFFER_SIZE];
static size_t s_uart_line_len;
static uint8_t s_uart2_line[IOT_ROUTER_LINE_BUFFER_SIZE];
static size_t s_uart2_line_len;
```

含义：
- `s_uart_line` 缓存 USART1 当前行。
- `s_uart2_line` 缓存 USART2 当前行。
- 遇到 `\r` 或 `\n` 后形成一个 packet。
- 如果超过 `IOT_ROUTER_LINE_BUFFER_SIZE`，强制提交当前 packet。

设计原因：
- 当前 shell 和测试命令都是文本行协议。
- OTA 关键字需要跨多个字节匹配，不能一个字节一个字节路由。
- 行缓存比完整 ringbuffer 简单，适合当前需求。

限制：
- 当前不适合二进制流透传。
- 二进制协议应改为长度帧、包头包尾或 ringbuffer parser。

### 11.3 `PortPacket`

```c
typedef struct {
    uint32_t source;
    size_t len;
    uint8_t data[IOT_ROUTER_LINE_BUFFER_SIZE];
} PortPacket;
```

字段说明：

| 字段 | 含义 |
| --- | --- |
| `source` | 数据来源，例如 UART2 |
| `len` | 数据长度 |
| `data[]` | 数据内容副本 |

设计原因：
- 中断里收到的数据不能只保存指针，因为行缓冲后续还会被继续写入。
- 入队时复制一份 packet，任务处理时数据稳定。
- packet 自带 source，核心层不需要知道它来自哪个硬件中断。

### 11.4 packet 队列

```c
static volatile uint8_t s_packet_head;
static volatile uint8_t s_packet_tail;
static PortPacket s_packet_queue[IOT_ROUTER_PACKET_QUEUE_SIZE];
```

这是一个小型环形队列。

设计原因：
- 中断只入队，不执行复杂规则。
- 任务出队后再路由。
- 环形队列静态分配，不使用 malloc。

当前队列大小：

```c
#define IOT_ROUTER_PACKET_QUEUE_SIZE 4U
```

注意：
- 当前队列满时直接丢弃新 packet。
- 后续如果高吞吐使用，应增加队列长度或改成 FreeRTOS queue/stream buffer。
- 如果多任务同时提交 USB/网络数据，应加临界区保护 head/tail。

### 11.5 `port_submit_line()`

```c
static void port_submit_line(uint32_t source,
                             uint8_t *buffer,
                             size_t *used,
                             uint8_t byte);
```

作用：
- 把中断收到的单字节追加到对应来源的行缓冲。
- 遇到换行时，将整行提交到 packet 队列。
- 行满时强制提交，避免缓冲溢出。

设计原因：
- 中断入口统一，不同 UART 只传不同 source 和 buffer。
- 避免 USART1/USART2 写两套重复逻辑。

### 11.6 `port_enqueue_packet()` 和 `port_dequeue_packet()`

入队：

```c
static void port_enqueue_packet(uint32_t source,
                                const uint8_t *data,
                                size_t len);
```

出队：

```c
static uint8_t port_dequeue_packet(PortPacket *packet);
```

设计原因：
- 入队函数在 ISR 或回调中调用，尽量短。
- 出队函数在任务中调用，可以执行耗时的规则匹配和输出。
- 复制数据而不是传指针，避免生命周期问题。

### 11.7 `port_uart_write()`

```c
static int port_uart_write(void *ctx, const uint8_t *data, size_t len);
```

当前实现：
- 忽略 `ctx`。
- 直接等待 `USART1->ISR & USART_ISR_TXE_TXFNF`。
- 写入 `USART1->TDR`。

设计说明：
- 当前 debug 输出走 COM3，简单可靠。
- 它是阻塞发送，所以不能在 ISR 中调用。
- 后续如果输出量大，应改为 DMA 或 TX ringbuffer。

### 11.8 `port_usb_write()`

```c
static int port_usb_write(void *ctx, const uint8_t *data, size_t len);
```

当前返回 `-1`，原因：
- 当前 USB 设备类是 RNDIS。
- 没有启用 USB CDC/VCP。
- 不能把它当作普通 USB 串口直接发送。

设计说明：
- USB channel 先保留，避免核心层后续再改。
- 当工程改成 CDC 或复合设备后，只需要实现该函数。
- 同时打开 `IOT_ROUTER_ENABLE_USB_STREAM`。

## 12. 默认规则配置

默认规则在 `IotRouter_PortInit()` 中注册。

### 12.1 channel 注册

```c
debug_channel.id = IOT_ROUTER_CHANNEL_DEBUG;
debug_channel.name = "debug";
debug_channel.write = port_uart_write;
IotRouter_AddChannel(&s_router, &debug_channel);
```

当前注册了四个 channel：

| channel | 当前实现 |
| --- | --- |
| `debug` | USART1/COM3 |
| `uart` | USART1/COM3 |
| `usb` | 预留，当前返回失败 |
| `ota` | USART1/COM3 |

### 12.2 `ota_detect`

```c
ota_rule.name = "ota_detect";
ota_rule.source_mask = IOT_ROUTER_SOURCE_UART |
                       IOT_ROUTER_SOURCE_UART2 |
                       IOT_ROUTER_SOURCE_USB;
ota_rule.contains = "ota";
ota_rule.output_mask = IOT_ROUTER_CHANNEL_OTA;
ota_rule.flags = IOT_ROUTER_RULE_CASE_INSENSITIVE |
                 IOT_ROUTER_RULE_TAG_SOURCE |
                 IOT_ROUTER_RULE_CONSUME_ON_MATCH;
ota_rule.action = port_ota_action;
```

含义：
- UART1、UART2、USB 中任何一路数据包含 `ota` 都会命中。
- 忽略大小写。
- 先执行 `port_ota_action()`。
- 再输出到 OTA channel。
- 命中后不再继续匹配普通转发规则。

当前 OTA action 只打印检测信息：

```text
[iot][ota] request detected from
uart2
```

后续可以在 `port_ota_action()` 中接入真正 OTA 状态机。

### 12.3 `uart_to_debug`

```c
uart_debug_rule.name = "uart_to_debug";
uart_debug_rule.source_mask = IOT_ROUTER_SOURCE_UART;
uart_debug_rule.output_mask = IOT_ROUTER_CHANNEL_DEBUG;
uart_debug_rule.flags = IOT_ROUTER_RULE_TAG_SOURCE;
```

含义：
- USART1 输入会以 `[uart] ...` 形式输出到 debug。
- 当前 USART1 同时作为 shell，所以这个规则主要用于验证旁路是否正常。

### 12.4 `uart2_to_debug`

```c
uart2_debug_rule.name = "uart2_to_debug";
uart2_debug_rule.source_mask = IOT_ROUTER_SOURCE_UART2;
uart2_debug_rule.output_mask = IOT_ROUTER_CHANNEL_DEBUG;
uart2_debug_rule.flags = IOT_ROUTER_RULE_TAG_SOURCE;
```

含义：
- USART2/COM8 输入会显示到 COM3。
- 当前用于验证第二路串口硬件链路。

示例：

```text
[uart2] verify_uart2_normal
```

### 12.5 `uart_to_usb`

```c
uart_usb_rule.name = "uart_to_usb";
uart_usb_rule.source_mask = IOT_ROUTER_SOURCE_UART;
uart_usb_rule.output_mask = IOT_ROUTER_CHANNEL_USB;
uart_usb_rule.flags = IOT_ROUTER_RULE_TAG_SOURCE;
```

当前注册后默认禁用：

```c
#if (IOT_ROUTER_ENABLE_USB_STREAM == 0U)
    IotRouter_SetRuleEnabled(&s_router, "uart_to_usb", 0U);
#endif
```

原因是当前 USB 是 RNDIS。后续切 CDC 后可以启用。

## 13. 调试验证方法

### 13.1 编译

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1
```

看到：

```text
[build] ok
```

表示 Keil 构建成功。脚本末尾可能出现 Conda GBK 编码报错，这是本机 PowerShell/Conda 环境问题，不影响 hex 生成。

### 13.2 烧录

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_keil.ps1
```

如果烧录后程序处于 halt，可执行：

```powershell
JLink.exe -CommandFile tools\reset_and_go.jlink
```

### 13.3 串口连接

```text
COM3: USART1，调试 shell 和 debug 输出，115200 8N1
COM8: USART2，第二路 IoT 输入，115200 8N1
```

USART2 硬件连接：

```text
外部 USB 转串口 TXD -> PA3 / USART2_RX
外部 USB 转串口 RXD -> PA2 / USART2_TX
外部 USB 转串口 GND -> GND
```

注意：
- 必须是 3.3V TTL 串口。
- 不要直接接 RS232 电平。

### 13.4 普通路由测试

从 `COM8` 发送：

```text
verify_uart2_normal
```

`COM3` 预期输出：

```text
[uart2] verify_uart2_normal
```

## 13.5 具体例子：COM8 发送 hello world 转发到 COM3

本节用一个完整例子说明 IoT Router 从接收、入队、规则匹配到输出的全过程。

测试目标：

```text
PC 串口工具 COM8 发送: hello world\r
MCU USART2 接收
IoT Router 标记来源为 uart2
匹配 uart2_to_debug 规则
通过 debug channel 从 USART1/COM3 输出: [uart2] hello world
```

### 13.5.1 硬件链路

当前第二路串口使用 `USART2`：

```text
PA2 = USART2_TX
PA3 = USART2_RX
```

实际连接：

```text
USB 转串口 TXD -> PA3 / USART2_RX
USB 转串口 RXD -> PA2 / USART2_TX
USB 转串口 GND -> GND
```

电脑端：

```text
COM8 -> USB 转串口 -> PA3 -> USART2_RX
COM3 <- USART1_TX <- debug channel 输出
```

注意这里有两个串口：
- `COM8` 是输入端，用来模拟外部 IoT 模块发送数据。
- `COM3` 是调试端，用来看 MCU 输出和 shell。

### 13.5.2 USART2 初始化

在 `main()` 中先初始化 USART2：

```c
USART1_Init();
USART2_Init();
heap_init();
comm_init_uart(NULL);
IotRouter_PortInit();
```

`USART2_Init()` 做了这些事：

```c
__HAL_RCC_GPIOA_CLK_ENABLE();
__HAL_RCC_USART2_CLK_ENABLE();
__HAL_RCC_USART234578_CONFIG(RCC_USART234578CLKSOURCE_D2PCLK1);
```

这几行打开 GPIOA、USART2 时钟，并选择 USART2 时钟源。

然后配置 PA2/PA3：

```c
GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
GPIO_InitStruct.Pull = GPIO_PULLUP;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

这表示：
- PA2/PA3 复用为 `GPIO_AF7_USART2`。
- PA2 是 USART2_TX。
- PA3 是 USART2_RX。

然后配置 USART2：

```c
USART2->BRR = HAL_RCC_GetPCLK1Freq() / kUartBaudrate;
USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_FIFOEN |
              USART_CR1_RXNEIE_RXFNEIE | USART_CR1_UE;
```

含义：
- `TE`：打开发送。
- `RE`：打开接收。
- `FIFOEN`：打开 FIFO。
- `RXNEIE_RXFNEIE`：收到数据后产生中断。
- `UE`：使能 USART。

最后打开 NVIC：

```c
HAL_NVIC_SetPriority(USART2_IRQn, 15U, 0U);
HAL_NVIC_EnableIRQ(USART2_IRQn);
```

这样 COM8 发出的每个字节都会触发 `USART2_IRQHandler()`。

### 13.5.3 COM8 发送数据

从电脑串口工具打开 `COM8`，发送：

```text
hello world
```

串口工具通常会在末尾发送回车，实际字节流可以理解为：

```text
h e l l o   w o r l d \r
```

也就是十六进制：

```text
68 65 6C 6C 6F 20 77 6F 72 6C 64 0D
```

这些字节从 USB 转串口芯片进入 MCU 的 PA3，也就是 `USART2_RX`。

### 13.5.4 USART2 中断接收

每收到一个字节，硬件置位 `RXNE_RXFNE`，进入：

```c
void USART2_IRQHandler(void)
{
  if ((USART2->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0U)
  {
    USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
  }

  while ((USART2->ISR & USART_ISR_RXNE_RXFNE) != 0U)
  {
    IotRouter_PortSubmitUart2Byte((uint8_t)USART2->RDR);
  }
}
```

流程解释：

1. 先检查串口错误。
   - `ORE`：溢出错误。
   - `FE`：帧错误。
   - `NE`：噪声错误。
   - `PE`：校验错误。

2. 如果有错误，写 `ICR` 清除错误标志。

3. 只要 RX FIFO 里有数据，就读取 `USART2->RDR`。

4. 每读出一个字节，就调用：

```c
IotRouter_PortSubmitUart2Byte(byte);
```

这里非常关键：中断函数不做规则匹配，也不做串口打印，只把字节提交给 port 层。

### 13.5.5 字节进入 uart2 行缓冲

`IotRouter_PortSubmitUart2Byte()` 的实现：

```c
void IotRouter_PortSubmitUart2Byte(uint8_t byte)
{
    if (s_router_ready == 0U) {
        return;
    }

    port_submit_line(IOT_ROUTER_SOURCE_UART2, s_uart2_line, &s_uart2_line_len, byte);
}
```

这里做了两件事：

1. 检查路由器是否初始化完成。
2. 把字节交给 `port_submit_line()`，并明确标记来源：

```c
IOT_ROUTER_SOURCE_UART2
```

这就是后面能输出 `[uart2]` 的来源。

`port_submit_line()` 会把收到的字节追加到 `s_uart2_line[]`：

```c
if (*used < IOT_ROUTER_LINE_BUFFER_SIZE) {
    buffer[*used] = byte;
    (*used)++;
}
```

当 COM8 依次发送 `hello world` 时，缓冲区变化如下：

```text
收到 h  -> "h"
收到 e  -> "he"
收到 l  -> "hel"
收到 l  -> "hell"
收到 o  -> "hello"
收到空格 -> "hello "
收到 w  -> "hello w"
...
收到 d  -> "hello world"
```

这时还没有提交路由，因为还没遇到换行。

### 13.5.6 收到回车后形成 packet

当收到 `\r` 时，进入这个分支：

```c
if ((byte == '\r') || (byte == '\n')) {
    if (*used != 0U) {
        port_enqueue_packet(source, buffer, *used);
        *used = 0U;
    }
    return;
}
```

此时：

```text
source = IOT_ROUTER_SOURCE_UART2
buffer = "hello world"
used   = 11
```

于是调用：

```c
port_enqueue_packet(IOT_ROUTER_SOURCE_UART2, s_uart2_line, 11);
```

然后 `s_uart2_line_len` 被清零，准备接收下一行。

### 13.5.7 packet 入队

`port_enqueue_packet()` 把这一行复制到环形队列：

```c
s_packet_queue[s_packet_head].source = source;
s_packet_queue[s_packet_head].len = copy_len;
memcpy(s_packet_queue[s_packet_head].data, data, copy_len);
s_packet_head = next_head;
```

入队后的 packet 内容：

```text
source = IOT_ROUTER_SOURCE_UART2
len    = 11
data   = "hello world"
```

为什么要复制数据：
- 中断返回后，`s_uart2_line[]` 会继续接收下一行。
- 如果队列里只保存指针，后续数据可能覆盖旧数据。
- 复制成 `PortPacket` 后，任务处理时数据是稳定的。

为什么中断里只入队：
- 后续规则匹配会遍历规则表。
- 输出到 COM3 会阻塞等待 USART1 TX。
- 这些都不适合在中断里做。

### 13.5.8 默认任务取出 packet

`StartDefaultTask()` 每 10 ms 调用：

```c
IotRouter_PortProcess();
```

`IotRouter_PortProcess()` 做出队：

```c
while (port_dequeue_packet(&packet) != 0U) {
    (void)IotRouter_RoutePacket(&s_router, packet.source, packet.data, packet.len);
}
```

刚才的 packet 被取出后，调用核心路由入口：

```c
IotRouter_RoutePacket(&s_router,
                      IOT_ROUTER_SOURCE_UART2,
                      (uint8_t *)"hello world",
                      11);
```

从这里开始进入可移植核心层 `iot_router.c`。

### 13.5.9 路由核心更新接收统计

`IotRouter_RoutePacket()` 开始时会更新统计：

```c
router->stats.rx_packets++;
router->stats.rx_bytes += (uint32_t)len;
```

对于本例：

```text
rx_packets + 1
rx_bytes   + 11
```

这些统计后续可以通过 `IotRouter_GetStats()` 读取。

### 13.5.10 遍历规则表

路由器按注册顺序遍历规则：

```c
for (uint8_t i = 0U; i < router->rule_count; i++) {
    IotRouterRule *rule = &router->rules[i];
    ...
}
```

当前默认规则顺序是：

```text
1. ota_detect
2. uart_to_debug
3. uart2_to_debug
4. usb_to_debug
5. uart_to_usb
```

### 13.5.11 检查 ota_detect 规则

第一条规则是：

```c
ota_rule.name = "ota_detect";
ota_rule.source_mask = IOT_ROUTER_SOURCE_UART |
                       IOT_ROUTER_SOURCE_UART2 |
                       IOT_ROUTER_SOURCE_USB;
ota_rule.contains = "ota";
```

检查过程：

1. 规则已启用，继续。
2. `source_mask` 包含 `IOT_ROUTER_SOURCE_UART2`，来源匹配。
3. 检查 data 是否包含 `"ota"`。

本例 data 是：

```text
hello world
```

不包含：

```text
ota
```

所以 `ota_detect` 不命中，继续下一条规则。

### 13.5.12 检查 uart_to_debug 规则

第二条规则：

```c
uart_debug_rule.source_mask = IOT_ROUTER_SOURCE_UART;
```

本例来源是：

```c
IOT_ROUTER_SOURCE_UART2
```

来源不匹配，所以跳过。

### 13.5.13 命中 uart2_to_debug 规则

第三条规则：

```c
uart2_debug_rule.name = "uart2_to_debug";
uart2_debug_rule.source_mask = IOT_ROUTER_SOURCE_UART2;
uart2_debug_rule.output_mask = IOT_ROUTER_CHANNEL_DEBUG;
uart2_debug_rule.flags = IOT_ROUTER_RULE_TAG_SOURCE;
```

检查过程：

1. 规则已启用。
2. `source_mask` 包含 `IOT_ROUTER_SOURCE_UART2`。
3. `contains` 为空，所以任意数据都匹配。

因此命中。

命中后更新规则统计：

```c
rule->match_count++;
```

然后因为没有 action，跳过 action：

```c
if (rule->action != NULL) {
    ...
}
```

接着按 `output_mask` 转发：

```c
IotRouter_Forward(router,
                  IOT_ROUTER_CHANNEL_DEBUG,
                  IOT_ROUTER_SOURCE_UART2,
                  (uint8_t *)"hello world",
                  11,
                  IOT_ROUTER_RULE_TAG_SOURCE);
```

### 13.5.14 找到 debug channel

`IotRouter_Forward()` 会遍历所有 channel：

```c
for (uint8_t i = 0U; i < router->channel_count; i++) {
    IotRouterChannel *channel = &router->channels[i];

    if ((channel->id & output_mask) == 0U) {
        continue;
    }
    ...
}
```

当前目标是：

```c
output_mask = IOT_ROUTER_CHANNEL_DEBUG
```

所以会找到这个通道：

```c
debug_channel.id = IOT_ROUTER_CHANNEL_DEBUG;
debug_channel.name = "debug";
debug_channel.write = port_uart_write;
```

也就是说，最终会调用：

```c
port_uart_write(...)
```

### 13.5.15 添加来源标签

因为规则设置了：

```c
IOT_ROUTER_RULE_TAG_SOURCE
```

所以 `IotRouter_Forward()` 会先输出来源标签。

它先获取 source 名称：

```c
const char *source_name = IotRouter_SourceName(source);
```

对于本例：

```c
source = IOT_ROUTER_SOURCE_UART2
```

所以返回：

```text
uart2
```

然后依次写出：

```text
[
uart2
] 
```

组合起来就是：

```text
[uart2] 
```

### 13.5.16 输出 payload

来源标签输出后，继续输出原始数据：

```c
channel->write(channel->ctx, data, len);
```

本例就是：

```text
hello world
```

最后追加换行：

```c
static const uint8_t newline[] = "\r\n";
channel->write(channel->ctx, newline, sizeof(newline) - 1U);
```

所以完整输出是：

```text
[uart2] hello world\r\n
```

### 13.5.17 debug channel 实际写入 USART1

debug channel 的 write 函数是：

```c
static int port_uart_write(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;

    if ((data == NULL) || (len == 0U)) {
        return 0;
    }

    for (size_t i = 0U; i < len; i++) {
        while ((USART1->ISR & USART_ISR_TXE_TXFNF) == 0U) {
        }
        USART1->TDR = data[i];
    }

    return 0;
}
```

它做的事情很直接：

1. 等待 USART1 TX FIFO 可以写。
2. 把字节写入 `USART1->TDR`。
3. 重复直到所有字节写完。

因为 USART1 连接到 COM3，所以电脑上的 COM3 会看到：

```text
[uart2] hello world
```

### 13.5.18 更新发送统计

发送成功后，channel 统计更新：

```c
channel->tx_packets++;
channel->tx_bytes += (uint32_t)len;
```

路由器整体转发统计也更新：

```c
router->stats.routed_packets++;
```

如果 `port_uart_write()` 返回失败，则会更新：

```c
channel->tx_errors++;
router->stats.dropped_packets++;
```

### 13.5.19 这个例子的完整调用链

完整链路如下：

```text
COM8 串口工具发送 "hello world\r"
  -> USB 转串口 TXD
  -> PA3 / USART2_RX
  -> USART2 硬件置位 RXNE_RXFNE
  -> USART2_IRQHandler()
  -> IotRouter_PortSubmitUart2Byte()
  -> port_submit_line(IOT_ROUTER_SOURCE_UART2, ...)
  -> 收到 \r 后 port_enqueue_packet()
  -> StartDefaultTask()
  -> IotRouter_PortProcess()
  -> port_dequeue_packet()
  -> IotRouter_RoutePacket(source=UART2, data="hello world")
  -> ota_detect 不命中
  -> uart_to_debug 不命中
  -> uart2_to_debug 命中
  -> IotRouter_Forward(output=DEBUG, flags=TAG_SOURCE)
  -> IotRouter_SourceName(UART2) 返回 "uart2"
  -> port_uart_write("[uart2] ")
  -> port_uart_write("hello world")
  -> port_uart_write("\r\n")
  -> USART1->TDR
  -> COM3 显示 "[uart2] hello world"
```

### 13.5.20 如果发送的是 OTA 命令，流程有什么不同

如果从 COM8 发送：

```text
start ota
```

前面的硬件接收、行缓冲、入队、出队流程完全一样。

区别发生在规则匹配阶段。

第一条规则 `ota_detect` 会命中：

```c
ota_rule.contains = "ota";
ota_rule.flags = IOT_ROUTER_RULE_CASE_INSENSITIVE |
                 IOT_ROUTER_RULE_TAG_SOURCE |
                 IOT_ROUTER_RULE_CONSUME_ON_MATCH;
```

命中后：

1. 执行 `port_ota_action()`。
2. 输出 OTA 检测信息。
3. 输出 OTA channel。
4. 因为设置了 `CONSUME_ON_MATCH`，不再继续匹配 `uart2_to_debug` 等普通规则。

预期输出：

```text
[iot][ota] request detected from
uart2
[uart2] start ota
```

这里 `[uart2] start ota` 是 OTA channel 输出，不是 `uart2_to_debug` 规则输出。

如果后续 OTA 要真正升级固件，应把 `port_ota_action()` 从“打印提示”替换成：

```text
解析 OTA 命令
进入 OTA 状态机
接收固件数据
写入 flash
校验
切换启动区或重启
```

## 13.6 代码流程图和内存示意图

本节继续使用 `COM8` 发送 `hello world\r` 的例子，把代码执行路径和关键内存状态画出来。

### 13.6.1 总体代码调用流程

```text
PC/COM8
  |
  |  "hello world\r"
  v
+-------------------+
| USB 转串口模块     |
+-------------------+
  |
  | TXD -> PA3
  v
+-------------------+       中断触发
| USART2 RX FIFO     | ----------------+
+-------------------+                 |
                                      v
                         +--------------------------+
                         | USART2_IRQHandler()      |
                         | 读 USART2->RDR           |
                         +--------------------------+
                                      |
                                      v
                         +--------------------------+
                         | IotRouter_PortSubmit     |
                         | Uart2Byte(byte)          |
                         +--------------------------+
                                      |
                                      v
                         +--------------------------+
                         | port_submit_line()       |
                         | 拼接到 s_uart2_line[]    |
                         +--------------------------+
                                      |
                         收到 '\r' 后 |
                                      v
                         +--------------------------+
                         | port_enqueue_packet()    |
                         | 复制到 s_packet_queue[]  |
                         +--------------------------+
                                      |
                                      | 任务 10ms 轮询
                                      v
                         +--------------------------+
                         | IotRouter_PortProcess()  |
                         | port_dequeue_packet()    |
                         +--------------------------+
                                      |
                                      v
                         +--------------------------+
                         | IotRouter_RoutePacket()  |
                         | 遍历规则表               |
                         +--------------------------+
                                      |
                         命中 uart2_to_debug
                                      |
                                      v
                         +--------------------------+
                         | IotRouter_Forward()      |
                         | 添加 [uart2] 标签        |
                         +--------------------------+
                                      |
                                      v
                         +--------------------------+
                         | port_uart_write()        |
                         | 写 USART1->TDR           |
                         +--------------------------+
                                      |
                                      v
                                  PC/COM3
                           "[uart2] hello world"
```

这张图对应两个重要设计点：
- 中断阶段只处理到 `port_enqueue_packet()`，不做打印。
- 真正路由和输出发生在 `IotRouter_PortProcess()` 所在任务中。

### 13.6.2 中断阶段代码流程

`USART2_IRQHandler()` 的执行逻辑可以简化成：

```text
USART2_IRQHandler()
  |
  +-- 检查 ORE/FE/NE/PE 错误
  |     |
  |     +-- 有错误则写 USART2->ICR 清除
  |
  +-- while RX FIFO 非空
        |
        +-- byte = USART2->RDR
        |
        +-- IotRouter_PortSubmitUart2Byte(byte)
```

对应代码：

```c
while ((USART2->ISR & USART_ISR_RXNE_RXFNE) != 0U)
{
  IotRouter_PortSubmitUart2Byte((uint8_t)USART2->RDR);
}
```

这里每次只取一个字节。例如 `hello world\r` 会触发若干次读 `RDR`：

```text
第 1 次: 'h'
第 2 次: 'e'
第 3 次: 'l'
...
第 11 次: 'd'
第 12 次: '\r'
```

### 13.6.3 行缓冲内存变化

`s_uart2_line[]` 是 USART2 的临时行缓冲：

```c
static uint8_t s_uart2_line[IOT_ROUTER_LINE_BUFFER_SIZE];
static size_t s_uart2_line_len;
```

假设刚开始：

```text
s_uart2_line_len = 0

s_uart2_line[]
index:  0   1   2   3   4   5   6   7   8   9   10
data : [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ]
```

收到 `hello` 后：

```text
s_uart2_line_len = 5

index:  0   1   2   3   4   5   6   7   8   9   10
data : [h] [e] [l] [l] [o] [ ] [ ] [ ] [ ] [ ] [ ]
```

收到完整 `hello world` 后，还没有遇到 `\r`：

```text
s_uart2_line_len = 11

index:  0   1   2   3   4   5   6   7   8   9   10
data : [h] [e] [l] [l] [o] [ ] [w] [o] [r] [l] [d]
```

注意 index 5 是空格字符，不是未使用空间。

收到 `\r` 后：

```text
port_enqueue_packet(IOT_ROUTER_SOURCE_UART2, s_uart2_line, 11);
s_uart2_line_len = 0;
```

此时行缓冲可以继续接收下一条命令。

### 13.6.4 packet 队列内存图

入队时会复制一份数据到 `s_packet_queue[]`：

```c
typedef struct {
    uint32_t source;
    size_t len;
    uint8_t data[IOT_ROUTER_LINE_BUFFER_SIZE];
} PortPacket;

static volatile uint8_t s_packet_head;
static volatile uint8_t s_packet_tail;
static PortPacket s_packet_queue[IOT_ROUTER_PACKET_QUEUE_SIZE];
```

假设队列大小为 4，初始状态：

```text
s_packet_head = 0
s_packet_tail = 0

s_packet_queue
slot 0: [empty]
slot 1: [empty]
slot 2: [empty]
slot 3: [empty]
```

`hello world` 入队后：

```text
s_packet_head = 1
s_packet_tail = 0

s_packet_queue
slot 0:
  source = IOT_ROUTER_SOURCE_UART2
  len    = 11
  data   = "hello world"

slot 1: [empty]  <- head 指向下一次写入位置
slot 2: [empty]
slot 3: [empty]
```

字符图：

```text
              tail
               |
               v
+--------+------------------------------+
| slot 0 | UART2, 11, "hello world"     |
+--------+------------------------------+
| slot 1 | empty                        | <- head
+--------+------------------------------+
| slot 2 | empty                        |
+--------+------------------------------+
| slot 3 | empty                        |
+--------+------------------------------+
```

任务调用 `port_dequeue_packet()` 后：

```text
packet = s_packet_queue[0]
s_packet_tail = 1
```

出队后：

```text
s_packet_head = 1
s_packet_tail = 1

队列重新为空。
```

这说明队列只负责 ISR 和任务之间的交接，不长期保存历史数据。

### 13.6.5 Router 内存关系图

`s_router` 内部保存 channel 表、rule 表和统计信息：

```text
s_router
|
+-- channels[0] debug
|     id    = IOT_ROUTER_CHANNEL_DEBUG
|     write = port_uart_write
|
+-- channels[1] uart
|     id    = IOT_ROUTER_CHANNEL_UART
|     write = port_uart_write
|
+-- channels[2] usb
|     id    = IOT_ROUTER_CHANNEL_USB
|     write = port_usb_write
|
+-- channels[3] ota
|     id    = IOT_ROUTER_CHANNEL_OTA
|     write = port_uart_write
|
+-- rules[0] ota_detect
|     source_mask = UART | UART2 | USB
|     contains    = "ota"
|     output_mask = OTA
|     action      = port_ota_action
|
+-- rules[1] uart_to_debug
|     source_mask = UART
|     contains    = NULL
|     output_mask = DEBUG
|
+-- rules[2] uart2_to_debug
|     source_mask = UART2
|     contains    = NULL
|     output_mask = DEBUG
|
+-- rules[3] usb_to_debug
|     source_mask = USB
|     contains    = NULL
|     output_mask = DEBUG
|
+-- rules[4] uart_to_usb
      source_mask = UART
      output_mask = USB
      当前默认禁用
```

当 `packet.source = UART2` 且 `packet.data = "hello world"` 时，规则匹配路径是：

```text
rules[0] ota_detect
  source 匹配 UART2
  contains("ota") 不匹配
  -> 跳过

rules[1] uart_to_debug
  source 不匹配 UART2
  -> 跳过

rules[2] uart2_to_debug
  source 匹配 UART2
  contains 为空，任意数据匹配
  -> 命中
```

命中后根据：

```text
output_mask = DEBUG
```

找到：

```text
channels[0] debug -> port_uart_write()
```

### 13.6.6 转发输出内存和字符拼接

命中 `uart2_to_debug` 后调用：

```c
IotRouter_Forward(router,
                  IOT_ROUTER_CHANNEL_DEBUG,
                  IOT_ROUTER_SOURCE_UART2,
                  data,
                  len,
                  IOT_ROUTER_RULE_TAG_SOURCE);
```

因为有 `IOT_ROUTER_RULE_TAG_SOURCE`，输出被拆成几段写入 channel：

```text
第 1 段: "["
第 2 段: "uart2"
第 3 段: "] "
第 4 段: "hello world"
第 5 段: "\r\n"
```

字符拼接效果：

```text
"[" + "uart2" + "] " + "hello world" + "\r\n"
        |
        v
"[uart2] hello world\r\n"
```

最终每个字节进入 `port_uart_write()`：

```text
[  u  a  r  t  2  ]     h  e  l  l  o     w  o  r  l  d  \r  \n
|  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |   |   |
v  v  v  v  v  v  v  v  v  v  v  v  v  v  v  v  v  v  v   v   v
USART1->TDR 逐字节发送
```

### 13.6.7 代码路径和数据状态对照表

| 阶段 | 函数 | source | data/byte | 内存位置 |
| --- | --- | --- | --- | --- |
| 接收字节 | `USART2_IRQHandler()` | 未封装 | `'h'` 等单字节 | `USART2->RDR` |
| 标记来源 | `IotRouter_PortSubmitUart2Byte()` | `UART2` | 单字节 | 函数参数 |
| 拼行 | `port_submit_line()` | `UART2` | `hello world` | `s_uart2_line[]` |
| 入队 | `port_enqueue_packet()` | `UART2` | `hello world` | `s_packet_queue[head]` |
| 出队 | `port_dequeue_packet()` | `UART2` | `hello world` | 局部变量 `packet` |
| 路由 | `IotRouter_RoutePacket()` | `UART2` | `hello world` | 核心层参数 |
| 规则命中 | `uart2_to_debug` | `UART2` | `hello world` | `s_router.rules[2]` |
| 找通道 | `IotRouter_Forward()` | `UART2` | `hello world` | `s_router.channels[0]` |
| 输出 | `port_uart_write()` | 已转成标签 | `[uart2] hello world` | `USART1->TDR` |

### 13.6.8 为什么这样拆分

这条链路看起来比直接在 `USART2_IRQHandler()` 里 `printf()` 更长，但它解决了几个工程问题：

```text
中断层:
  只负责收字节和入队。

port 层:
  只负责把硬件数据转换成统一 packet。

core 层:
  只负责规则匹配和通道转发。

channel:
  只负责具体输出方式。

action:
  只负责特殊业务处理，例如 OTA。
```

因此后续增加新需求时，改动范围更小：
- 新增串口：加 source、port submit、IRQ、规则。
- 新增转发目标：加 channel。
- 新增特殊处理：加 action 和 rule。
- 换平台：保留 core，替换 port。

### 13.7 OTA 测试

从 `COM8` 发送：

```text
verify_OTA_from_uart2
```

`COM3` 预期输出：

```text
[iot][ota] request detected from
uart2
[uart2] verify_OTA_from_uart2
```

### 13.8 shell 兼容性测试

从 `COM3` 发送：

```text
help
```

预期：

```text
commands: help mem w25q pwd ls cat touch write mkdir cd vfs vim reboot
> [uart] help
```

这说明：
- shell 仍然正常响应。
- USART1 输入也被路由器旁路标记为 `uart`。

## 14. 扩展新来源

以新增 UART3 为例。

### 14.1 增加 source bit

```c
#define IOT_ROUTER_SOURCE_UART3 (1UL << 4)
```

### 14.2 增加名称映射

```c
if ((source & IOT_ROUTER_SOURCE_UART3) != 0U) {
    return "uart3";
}
```

### 14.3 增加 port 缓冲和提交函数

```c
static uint8_t s_uart3_line[IOT_ROUTER_LINE_BUFFER_SIZE];
static size_t s_uart3_line_len;

void IotRouter_PortSubmitUart3Byte(uint8_t byte)
{
    if (s_router_ready == 0U) {
        return;
    }

    port_submit_line(IOT_ROUTER_SOURCE_UART3,
                     s_uart3_line,
                     &s_uart3_line_len,
                     byte);
}
```

### 14.4 增加中断入口

```c
void USART3_IRQHandler(void)
{
    while ((USART3->ISR & USART_ISR_RXNE_RXFNE) != 0U) {
        IotRouter_PortSubmitUart3Byte((uint8_t)USART3->RDR);
    }
}
```

### 14.5 增加规则

```c
uart3_debug_rule.name = "uart3_to_debug";
uart3_debug_rule.source_mask = IOT_ROUTER_SOURCE_UART3;
uart3_debug_rule.output_mask = IOT_ROUTER_CHANNEL_DEBUG;
uart3_debug_rule.flags = IOT_ROUTER_RULE_TAG_SOURCE;
IotRouter_AddRule(&s_router, &uart3_debug_rule);
```

## 15. 扩展新处理逻辑

如果要增加新的特殊处理，例如配置命令、AT 指令、JSON 命令、日志上报，可以新增 action。

### 15.1 action 函数模板

```c
static IotRouterStatus port_custom_action(IotRouter *router,
                                          const IotRouterRule *rule,
                                          uint32_t source,
                                          const uint8_t *data,
                                          size_t len,
                                          void *ctx)
{
    (void)rule;
    (void)ctx;

    /* 解析 data，执行业务动作 */

    IotRouter_Forward(router,
                      IOT_ROUTER_CHANNEL_DEBUG,
                      source,
                      (const uint8_t *)"custom command handled",
                      strlen("custom command handled"),
                      IOT_ROUTER_RULE_TAG_SOURCE);

    return IOT_ROUTER_OK;
}
```

### 15.2 注册规则

```c
custom_rule.name = "custom_cmd";
custom_rule.source_mask = IOT_ROUTER_SOURCE_UART2;
custom_rule.contains = "cmd";
custom_rule.output_mask = IOT_ROUTER_CHANNEL_DEBUG;
custom_rule.flags = IOT_ROUTER_RULE_CASE_INSENSITIVE |
                    IOT_ROUTER_RULE_TAG_SOURCE |
                    IOT_ROUTER_RULE_CONSUME_ON_MATCH;
custom_rule.action = port_custom_action;
IotRouter_AddRule(&s_router, &custom_rule);
```

是否设置 `CONSUME_ON_MATCH` 的判断：
- 设置：特殊命令被处理后，不继续被普通规则转发。
- 不设置：特殊命令处理后，还会继续走后续规则。

## 16. 移植到其他平台

核心层 `iot_router.c/.h` 可以直接复用。

移植时通常只需要重写 port 层：

```text
IotRouter_PortInit()
  注册当前平台的 channel 和 rule。

IotRouter_PortSubmitXxx()
  从 UART、USB、CAN、BLE、TCP 等输入提交数据。

IotRouter_PortProcess()
  在主循环、RTOS task 或事件线程中执行路由。

port_xxx_write()
  实现不同输出通道的实际发送。
```

裸机工程：
- 可以在主循环中周期调用 `IotRouter_PortProcess()`。
- 中断中只入队。

FreeRTOS 工程：
- 可以用专门 task 阻塞等待 queue。
- 当前工程为了简单，在 `StartDefaultTask()` 中 10 ms 轮询。

高吞吐工程：
- 应使用 ringbuffer、DMA idle line、FreeRTOS stream buffer。
- 不建议继续使用当前 4 包小队列。

二进制协议：
- 不要使用 `TAG_SOURCE`。
- 不要用 `contains` 做协议解析。
- 建议在 port 层先解析完整帧，再把完整帧交给 `IotRouter_RoutePacket()`。

## 17. 当前实现限制和后续建议

当前实现适合：
- 串口文本命令。
- 调试日志路由。
- OTA 触发关键字检测。
- 多来源输入的快速验证。

当前限制：
- packet 队列满时会丢包。
- 队列 head/tail 没有显式临界区，当前主要由 ISR 入队、单任务出队，简单场景可用。
- `port_uart_write()` 是阻塞发送。
- USB channel 当前未真正发送，因为 USB 是 RNDIS。
- `contains` 是简单子串匹配，不适合复杂协议。

后续建议：
- 增加 shell 命令查看 `IotRouterStats`、rule 命中次数和 channel 错误次数。
- 将 `port_uart_write()` 改成 TX ringbuffer 或 DMA。
- 对 packet 队列增加临界区保护。
- USB 切到 CDC 或复合设备后实现 `port_usb_write()`。
- OTA action 接入真实 OTA 状态机、flash 写入和校验流程。

