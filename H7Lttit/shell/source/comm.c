#include "comm.h"

#include "iot_router_port.h"
#include "main.h"

comm_t *comm;

#define UART_RX_BUFFER_SIZE 256U

static volatile uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t uart_rx_head;
static volatile uint16_t uart_rx_tail;

static void uart_rx_push(uint8_t ch)
{
    uint16_t next = (uint16_t)((uart_rx_head + 1U) % UART_RX_BUFFER_SIZE);

    if (next != uart_rx_tail) {
        uart_rx_buffer[uart_rx_head] = ch;
        uart_rx_head = next;
    }
}

static int uart_rx_available(void)
{
    if ((USART1->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0U) {
        USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    }

    if ((USART1->ISR & USART_ISR_RXNE_RXFNE) != 0U) {
        uart_rx_push((uint8_t)USART1->RDR);
    }

    return (uart_rx_head != uart_rx_tail) ? 1 : 0;
}

static void uart_tx_putc(char c)
{
    while ((USART1->ISR & USART_ISR_TXE_TXFNF) == 0U) {
    }
    USART1->TDR = (uint8_t)c;
}

static char uart_rx_pop(void)
{
    uint8_t ch;

    while (uart_rx_available() == 0) {
    }

    ch = uart_rx_buffer[uart_rx_tail];
    uart_rx_tail = (uint16_t)((uart_rx_tail + 1U) % UART_RX_BUFFER_SIZE);
    return (char)ch;
}

static void uart_putc(void *ctx, char c)
{
    (void)ctx;
    uart_tx_putc(c);
}

static char uart_getc(void *ctx)
{
    (void)ctx;
    return uart_rx_pop();
}

static void uart_write(void *ctx, const char *buf, int len)
{
    (void)ctx;

    if ((buf == NULL) || (len <= 0)) {
        return;
    }

    for (int i = 0; i < len; i++) {
        uart_tx_putc(buf[i]);
    }
}

static int uart_peek(void *ctx)
{
    (void)ctx;
    return (uart_rx_available() != 0) ? 1 : -1;
}

static comm_t uart_comm = {
    .putc = uart_putc,
    .getc = uart_getc,
    .write = uart_write,
    .peek = uart_peek,
    .ctx = NULL,
};

void comm_init_uart(void *ctx)
{
    uart_comm.ctx = ctx;
    comm = &uart_comm;
}

void comm_uart_irq_handler(void)
{
    if ((USART1->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0U) {
        USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    }

    while ((USART1->ISR & USART_ISR_RXNE_RXFNE) != 0U) {
        uint8_t ch = (uint8_t)USART1->RDR;

        uart_rx_push(ch);
        IotRouter_PortSubmitUartByte(ch);
    }
}
