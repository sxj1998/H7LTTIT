#ifndef IOT_ROUTER_H
#define IOT_ROUTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef IOT_ROUTER_MAX_CHANNELS
#define IOT_ROUTER_MAX_CHANNELS 8U
#endif

#ifndef IOT_ROUTER_MAX_RULES
#define IOT_ROUTER_MAX_RULES 8U
#endif

#define IOT_ROUTER_SOURCE_UART  (1UL << 0)
#define IOT_ROUTER_SOURCE_USB   (1UL << 1)
#define IOT_ROUTER_SOURCE_APP   (1UL << 2)
#define IOT_ROUTER_SOURCE_UART2 (1UL << 3)

#define IOT_ROUTER_CHANNEL_DEBUG (1UL << 0)
#define IOT_ROUTER_CHANNEL_UART  (1UL << 1)
#define IOT_ROUTER_CHANNEL_USB   (1UL << 2)
#define IOT_ROUTER_CHANNEL_OTA   (1UL << 3)
#define IOT_ROUTER_CHANNEL_SCREEN (1UL << 4)

#define IOT_ROUTER_RULE_TAG_SOURCE       (1UL << 0)
#define IOT_ROUTER_RULE_CONSUME_ON_MATCH (1UL << 1)
#define IOT_ROUTER_RULE_CASE_INSENSITIVE (1UL << 2)
#define IOT_ROUTER_RULE_ENABLED          (1UL << 31)

typedef enum {
    IOT_ROUTER_OK = 0,
    IOT_ROUTER_EINVAL = -1,
    IOT_ROUTER_ENOSPC = -2,
    IOT_ROUTER_EIO = -3,
} IotRouterStatus;

struct IotRouter;
struct IotRouterRule;

typedef int (*IotRouterWriteFn)(void *ctx, const uint8_t *data, size_t len);
typedef IotRouterStatus (*IotRouterActionFn)(struct IotRouter *router,
                                             const struct IotRouterRule *rule,
                                             uint32_t source,
                                             const uint8_t *data,
                                             size_t len,
                                             void *ctx);

typedef struct {
    uint32_t id;
    const char *name;
    IotRouterWriteFn write;
    void *ctx;
    uint32_t tx_packets;
    uint32_t tx_bytes;
    uint32_t tx_errors;
} IotRouterChannel;

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

typedef struct {
    uint32_t rx_packets;
    uint32_t rx_bytes;
    uint32_t routed_packets;
    uint32_t dropped_packets;
} IotRouterStats;

typedef struct IotRouter {
    IotRouterChannel channels[IOT_ROUTER_MAX_CHANNELS];
    IotRouterRule rules[IOT_ROUTER_MAX_RULES];
    IotRouterStats stats;
    uint8_t channel_count;
    uint8_t rule_count;
} IotRouter;

void IotRouter_Init(IotRouter *router);
IotRouterStatus IotRouter_AddChannel(IotRouter *router, const IotRouterChannel *channel);
IotRouterStatus IotRouter_AddRule(IotRouter *router, const IotRouterRule *rule);
IotRouterStatus IotRouter_SetRuleEnabled(IotRouter *router, const char *name, uint8_t enabled);
IotRouterStatus IotRouter_RoutePacket(IotRouter *router, uint32_t source, const uint8_t *data, size_t len);
IotRouterStatus IotRouter_Forward(IotRouter *router,
                                  uint32_t output_mask,
                                  uint32_t source,
                                  const uint8_t *data,
                                  size_t len,
                                  uint32_t flags);
const char *IotRouter_SourceName(uint32_t source);
const IotRouterStats *IotRouter_GetStats(const IotRouter *router);

#ifdef __cplusplus
}
#endif

#endif
