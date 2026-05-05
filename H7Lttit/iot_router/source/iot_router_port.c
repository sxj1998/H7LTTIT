#include "iot_router_port.h"

#include "lv_port_lvgl.h"
#include "main.h"

#include <string.h>

#ifndef IOT_ROUTER_LINE_BUFFER_SIZE
#define IOT_ROUTER_LINE_BUFFER_SIZE 128U
#endif

#ifndef IOT_ROUTER_PACKET_QUEUE_SIZE
#define IOT_ROUTER_PACKET_QUEUE_SIZE 4U
#endif

#ifndef IOT_ROUTER_ENABLE_DEMO_FORWARD
#define IOT_ROUTER_ENABLE_DEMO_FORWARD 1U
#endif

#ifndef IOT_ROUTER_ENABLE_USB_STREAM
#define IOT_ROUTER_ENABLE_USB_STREAM 0U
#endif

static IotRouter s_router;
static volatile uint8_t s_router_ready;
static uint8_t s_uart_line[IOT_ROUTER_LINE_BUFFER_SIZE];
static size_t s_uart_line_len;
static uint8_t s_uart2_line[IOT_ROUTER_LINE_BUFFER_SIZE];
static size_t s_uart2_line_len;
static char s_screen_line[IOT_ROUTER_LINE_BUFFER_SIZE];
static size_t s_screen_line_len;
static volatile uint8_t s_screen_line_pending;

typedef struct {
    uint32_t source;
    size_t len;
    uint8_t data[IOT_ROUTER_LINE_BUFFER_SIZE];
} PortPacket;

static volatile uint8_t s_packet_head;
static volatile uint8_t s_packet_tail;
static PortPacket s_packet_queue[IOT_ROUTER_PACKET_QUEUE_SIZE];

static int port_uart_write(void *ctx, const uint8_t *data, size_t len);
static int port_usb_write(void *ctx, const uint8_t *data, size_t len);
static int port_screen_write(void *ctx, const uint8_t *data, size_t len);
static IotRouterStatus port_ota_action(IotRouter *router,
                                        const IotRouterRule *rule,
                                        uint32_t source,
                                        const uint8_t *data,
                                        size_t len,
                                        void *ctx);
static void port_submit_line(uint32_t source, uint8_t *buffer, size_t *used, uint8_t byte);
static void port_enqueue_packet(uint32_t source, const uint8_t *data, size_t len);
static uint8_t port_dequeue_packet(PortPacket *packet);

void IotRouter_PortInit(void)
{
    IotRouterChannel debug_channel;
    IotRouterChannel uart_channel;
    IotRouterChannel usb_channel;
    IotRouterChannel ota_channel;
    IotRouterChannel screen_channel;
    IotRouterRule ota_rule;
    IotRouterRule uart_debug_rule;
    IotRouterRule uart2_debug_rule;
    IotRouterRule uart2_screen_rule;
    IotRouterRule usb_debug_rule;
    IotRouterRule uart_usb_rule;

    IotRouter_Init(&s_router);

    memset(&debug_channel, 0, sizeof(debug_channel));
    debug_channel.id = IOT_ROUTER_CHANNEL_DEBUG;
    debug_channel.name = "debug";
    debug_channel.write = port_uart_write;
    (void)IotRouter_AddChannel(&s_router, &debug_channel);

    memset(&uart_channel, 0, sizeof(uart_channel));
    uart_channel.id = IOT_ROUTER_CHANNEL_UART;
    uart_channel.name = "uart";
    uart_channel.write = port_uart_write;
    (void)IotRouter_AddChannel(&s_router, &uart_channel);

    memset(&usb_channel, 0, sizeof(usb_channel));
    usb_channel.id = IOT_ROUTER_CHANNEL_USB;
    usb_channel.name = "usb";
    usb_channel.write = port_usb_write;
    (void)IotRouter_AddChannel(&s_router, &usb_channel);

    memset(&ota_channel, 0, sizeof(ota_channel));
    ota_channel.id = IOT_ROUTER_CHANNEL_OTA;
    ota_channel.name = "ota";
    ota_channel.write = port_uart_write;
    (void)IotRouter_AddChannel(&s_router, &ota_channel);

    memset(&screen_channel, 0, sizeof(screen_channel));
    screen_channel.id = IOT_ROUTER_CHANNEL_SCREEN;
    screen_channel.name = "screen";
    screen_channel.write = port_screen_write;
    (void)IotRouter_AddChannel(&s_router, &screen_channel);

    memset(&ota_rule, 0, sizeof(ota_rule));
    ota_rule.name = "ota_detect";
    ota_rule.source_mask = IOT_ROUTER_SOURCE_UART | IOT_ROUTER_SOURCE_UART2 | IOT_ROUTER_SOURCE_USB;
    ota_rule.contains = "ota";
    ota_rule.output_mask = IOT_ROUTER_CHANNEL_OTA | IOT_ROUTER_CHANNEL_SCREEN;
    ota_rule.flags = IOT_ROUTER_RULE_CASE_INSENSITIVE |
                     IOT_ROUTER_RULE_TAG_SOURCE |
                     IOT_ROUTER_RULE_CONSUME_ON_MATCH;
    ota_rule.action = port_ota_action;
    (void)IotRouter_AddRule(&s_router, &ota_rule);

    memset(&uart_debug_rule, 0, sizeof(uart_debug_rule));
    uart_debug_rule.name = "uart_to_debug";
    uart_debug_rule.source_mask = IOT_ROUTER_SOURCE_UART;
    uart_debug_rule.output_mask = IOT_ROUTER_CHANNEL_DEBUG;
    uart_debug_rule.flags = IOT_ROUTER_RULE_TAG_SOURCE;
    (void)IotRouter_AddRule(&s_router, &uart_debug_rule);

    memset(&uart2_debug_rule, 0, sizeof(uart2_debug_rule));
    uart2_debug_rule.name = "uart2_to_debug";
    uart2_debug_rule.source_mask = IOT_ROUTER_SOURCE_UART2;
    uart2_debug_rule.output_mask = IOT_ROUTER_CHANNEL_DEBUG;
    uart2_debug_rule.flags = IOT_ROUTER_RULE_TAG_SOURCE;
    (void)IotRouter_AddRule(&s_router, &uart2_debug_rule);

    memset(&uart2_screen_rule, 0, sizeof(uart2_screen_rule));
    uart2_screen_rule.name = "uart2_to_screen";
    uart2_screen_rule.source_mask = IOT_ROUTER_SOURCE_UART2;
    uart2_screen_rule.output_mask = IOT_ROUTER_CHANNEL_SCREEN;
    (void)IotRouter_AddRule(&s_router, &uart2_screen_rule);

    memset(&usb_debug_rule, 0, sizeof(usb_debug_rule));
    usb_debug_rule.name = "usb_to_debug";
    usb_debug_rule.source_mask = IOT_ROUTER_SOURCE_USB;
    usb_debug_rule.output_mask = IOT_ROUTER_CHANNEL_DEBUG;
    usb_debug_rule.flags = IOT_ROUTER_RULE_TAG_SOURCE;
    (void)IotRouter_AddRule(&s_router, &usb_debug_rule);

    memset(&uart_usb_rule, 0, sizeof(uart_usb_rule));
    uart_usb_rule.name = "uart_to_usb";
    uart_usb_rule.source_mask = IOT_ROUTER_SOURCE_UART;
    uart_usb_rule.output_mask = IOT_ROUTER_CHANNEL_USB;
    uart_usb_rule.flags = IOT_ROUTER_RULE_TAG_SOURCE;
    (void)IotRouter_AddRule(&s_router, &uart_usb_rule);

#if (IOT_ROUTER_ENABLE_DEMO_FORWARD == 0U)
    (void)IotRouter_SetRuleEnabled(&s_router, "uart_to_debug", 0U);
    (void)IotRouter_SetRuleEnabled(&s_router, "uart2_to_debug", 0U);
    (void)IotRouter_SetRuleEnabled(&s_router, "uart2_to_screen", 0U);
    (void)IotRouter_SetRuleEnabled(&s_router, "usb_to_debug", 0U);
#endif

#if (IOT_ROUTER_ENABLE_USB_STREAM == 0U)
    (void)IotRouter_SetRuleEnabled(&s_router, "uart_to_usb", 0U);
#endif

    s_uart_line_len = 0U;
    s_uart2_line_len = 0U;
    s_screen_line_len = 0U;
    s_screen_line_pending = 0U;
    s_packet_head = 0U;
    s_packet_tail = 0U;
    s_router_ready = 1U;
}

void IotRouter_PortProcess(void)
{
    PortPacket packet;

    if (s_router_ready == 0U) {
        return;
    }

    while (port_dequeue_packet(&packet) != 0U) {
        (void)IotRouter_RoutePacket(&s_router, packet.source, packet.data, packet.len);
    }
}

void IotRouter_PortDisplayProcess(void)
{
    char line[IOT_ROUTER_LINE_BUFFER_SIZE];

    if (s_screen_line_pending == 0U) {
        return;
    }

    memcpy(line, s_screen_line, sizeof(line));
    line[sizeof(line) - 1U] = '\0';
    s_screen_line_pending = 0U;

    LvPort_TerminalPushLine(line);
}

void IotRouter_PortSubmitUartByte(uint8_t byte)
{
    if (s_router_ready == 0U) {
        return;
    }

    port_submit_line(IOT_ROUTER_SOURCE_UART, s_uart_line, &s_uart_line_len, byte);
}

void IotRouter_PortSubmitUart2Byte(uint8_t byte)
{
    if (s_router_ready == 0U) {
        return;
    }

    port_submit_line(IOT_ROUTER_SOURCE_UART2, s_uart2_line, &s_uart2_line_len, byte);
}

void IotRouter_PortSubmitUsbData(const uint8_t *data, size_t len)
{
    if ((s_router_ready == 0U) || (data == NULL) || (len == 0U)) {
        return;
    }

    port_enqueue_packet(IOT_ROUTER_SOURCE_USB, data, len);
}

IotRouter *IotRouter_PortGetRouter(void)
{
    return &s_router;
}

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

static int port_usb_write(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    (void)data;
    (void)len;

    /*
     * The current firmware enumerates USB as RNDIS. Keep the USB stream
     * channel as a port hook so a CDC/VCP build can implement it without
     * touching the portable router core.
     */
    return -1;
}

static int port_screen_write(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;

    if ((data == NULL) || (len == 0U)) {
        return 0;
    }

    for (size_t i = 0U; i < len; i++) {
        uint8_t ch = data[i];

        if ((ch == '\r') || (ch == '\n')) {
            if (s_screen_line_len != 0U) {
                s_screen_line[s_screen_line_len] = '\0';
                s_screen_line_pending = 1U;
                s_screen_line_len = 0U;
            }
            continue;
        }

        if (s_screen_line_len < (sizeof(s_screen_line) - 1U)) {
            s_screen_line[s_screen_line_len] = (char)ch;
            s_screen_line_len++;
        }
    }

    return 0;
}

static IotRouterStatus port_ota_action(IotRouter *router,
                                        const IotRouterRule *rule,
                                        uint32_t source,
                                        const uint8_t *data,
                                        size_t len,
                                        void *ctx)
{
    static const uint8_t message[] = "[iot][ota] request detected from ";
    static const uint8_t start_message[] = "[iot][ota] ota start";
    const char *source_name;

    (void)rule;
    (void)data;
    (void)len;
    (void)ctx;

    source_name = IotRouter_SourceName(source);
    (void)IotRouter_Forward(router,
                            IOT_ROUTER_CHANNEL_DEBUG,
                            IOT_ROUTER_SOURCE_APP,
                            message,
                            sizeof(message) - 1U,
                            0U);
    (void)IotRouter_Forward(router,
                            IOT_ROUTER_CHANNEL_DEBUG,
                            IOT_ROUTER_SOURCE_APP,
                            (const uint8_t *)source_name,
                            strlen(source_name),
                            0U);
    (void)IotRouter_Forward(router,
                            IOT_ROUTER_CHANNEL_DEBUG,
                            IOT_ROUTER_SOURCE_APP,
                            start_message,
                            sizeof(start_message) - 1U,
                            0U);
    return IOT_ROUTER_OK;
}

static void port_submit_line(uint32_t source, uint8_t *buffer, size_t *used, uint8_t byte)
{
    if ((buffer == NULL) || (used == NULL)) {
        return;
    }

    if ((byte == '\r') || (byte == '\n')) {
        if (*used != 0U) {
            port_enqueue_packet(source, buffer, *used);
            *used = 0U;
        }
        return;
    }

    if (*used < IOT_ROUTER_LINE_BUFFER_SIZE) {
        buffer[*used] = byte;
        (*used)++;
    }

    if (*used >= IOT_ROUTER_LINE_BUFFER_SIZE) {
        port_enqueue_packet(source, buffer, *used);
        *used = 0U;
    }
}

static void port_enqueue_packet(uint32_t source, const uint8_t *data, size_t len)
{
    uint8_t next_head;
    size_t copy_len;

    if ((data == NULL) || (len == 0U)) {
        return;
    }

    next_head = (uint8_t)((s_packet_head + 1U) % IOT_ROUTER_PACKET_QUEUE_SIZE);
    if (next_head == s_packet_tail) {
        return;
    }

    copy_len = len;
    if (copy_len > IOT_ROUTER_LINE_BUFFER_SIZE) {
        copy_len = IOT_ROUTER_LINE_BUFFER_SIZE;
    }

    s_packet_queue[s_packet_head].source = source;
    s_packet_queue[s_packet_head].len = copy_len;
    memcpy(s_packet_queue[s_packet_head].data, data, copy_len);
    s_packet_head = next_head;
}

static uint8_t port_dequeue_packet(PortPacket *packet)
{
    if ((packet == NULL) || (s_packet_tail == s_packet_head)) {
        return 0U;
    }

    *packet = s_packet_queue[s_packet_tail];
    s_packet_tail = (uint8_t)((s_packet_tail + 1U) % IOT_ROUTER_PACKET_QUEUE_SIZE);
    return 1U;
}
