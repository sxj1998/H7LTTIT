#ifndef IOT_ROUTER_PORT_H
#define IOT_ROUTER_PORT_H

#include "iot_router.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void IotRouter_PortInit(void);
void IotRouter_PortProcess(void);
void IotRouter_PortSubmitUartByte(uint8_t byte);
void IotRouter_PortSubmitUart2Byte(uint8_t byte);
void IotRouter_PortSubmitUsbData(const uint8_t *data, size_t len);
IotRouter *IotRouter_PortGetRouter(void);

#ifdef __cplusplus
}
#endif

#endif
