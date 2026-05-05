#include "iot_router.h"

#include <string.h>

static int router_char_equal(char a, char b, uint32_t flags)
{
    if ((flags & IOT_ROUTER_RULE_CASE_INSENSITIVE) == 0U) {
        return (a == b) ? 1 : 0;
    }

    if ((a >= 'A') && (a <= 'Z')) {
        a = (char)(a - 'A' + 'a');
    }
    if ((b >= 'A') && (b <= 'Z')) {
        b = (char)(b - 'A' + 'a');
    }

    return (a == b) ? 1 : 0;
}

static int router_contains(const uint8_t *data, size_t len, const char *pattern, uint32_t flags)
{
    size_t pattern_len;

    if ((pattern == NULL) || (pattern[0] == '\0')) {
        return 1;
    }

    pattern_len = strlen(pattern);
    if ((data == NULL) || (len < pattern_len)) {
        return 0;
    }

    for (size_t i = 0U; i <= (len - pattern_len); i++) {
        size_t j;

        for (j = 0U; j < pattern_len; j++) {
            if (router_char_equal((char)data[i + j], pattern[j], flags) == 0) {
                break;
            }
        }

        if (j == pattern_len) {
            return 1;
        }
    }

    return 0;
}

static IotRouterChannel *router_find_channel(IotRouter *router, uint32_t channel_id)
{
    for (uint8_t i = 0U; i < router->channel_count; i++) {
        if (router->channels[i].id == channel_id) {
            return &router->channels[i];
        }
    }

    return NULL;
}

static IotRouterRule *router_find_rule(IotRouter *router, const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    for (uint8_t i = 0U; i < router->rule_count; i++) {
        if ((router->rules[i].name != NULL) && (strcmp(router->rules[i].name, name) == 0)) {
            return &router->rules[i];
        }
    }

    return NULL;
}

void IotRouter_Init(IotRouter *router)
{
    if (router == NULL) {
        return;
    }

    memset(router, 0, sizeof(*router));
}

IotRouterStatus IotRouter_AddChannel(IotRouter *router, const IotRouterChannel *channel)
{
    if ((router == NULL) || (channel == NULL) || (channel->id == 0U) || (channel->write == NULL)) {
        return IOT_ROUTER_EINVAL;
    }

    if (router_find_channel(router, channel->id) != NULL) {
        return IOT_ROUTER_EINVAL;
    }

    if (router->channel_count >= IOT_ROUTER_MAX_CHANNELS) {
        return IOT_ROUTER_ENOSPC;
    }

    router->channels[router->channel_count] = *channel;
    router->channel_count++;
    return IOT_ROUTER_OK;
}

IotRouterStatus IotRouter_AddRule(IotRouter *router, const IotRouterRule *rule)
{
    if ((router == NULL) || (rule == NULL) || (rule->name == NULL) || (rule->source_mask == 0U)) {
        return IOT_ROUTER_EINVAL;
    }

    if (router_find_rule(router, rule->name) != NULL) {
        return IOT_ROUTER_EINVAL;
    }

    if (router->rule_count >= IOT_ROUTER_MAX_RULES) {
        return IOT_ROUTER_ENOSPC;
    }

    router->rules[router->rule_count] = *rule;
    router->rules[router->rule_count].flags |= IOT_ROUTER_RULE_ENABLED;
    router->rule_count++;
    return IOT_ROUTER_OK;
}

IotRouterStatus IotRouter_SetRuleEnabled(IotRouter *router, const char *name, uint8_t enabled)
{
    IotRouterRule *rule = router_find_rule(router, name);

    if (rule == NULL) {
        return IOT_ROUTER_EINVAL;
    }

    if (enabled != 0U) {
        rule->flags |= IOT_ROUTER_RULE_ENABLED;
    } else {
        rule->flags &= ~IOT_ROUTER_RULE_ENABLED;
    }

    return IOT_ROUTER_OK;
}

IotRouterStatus IotRouter_Forward(IotRouter *router,
                                  uint32_t output_mask,
                                  uint32_t source,
                                  const uint8_t *data,
                                  size_t len,
                                  uint32_t flags)
{
    IotRouterStatus status = IOT_ROUTER_OK;

    if ((router == NULL) || (data == NULL) || (len == 0U)) {
        return IOT_ROUTER_EINVAL;
    }

    for (uint8_t i = 0U; i < router->channel_count; i++) {
        IotRouterChannel *channel = &router->channels[i];
        int write_status = 0;

        if ((channel->id & output_mask) == 0U) {
            continue;
        }

        if ((flags & IOT_ROUTER_RULE_TAG_SOURCE) != 0U) {
            const char *source_name = IotRouter_SourceName(source);
            static const uint8_t prefix_open[] = "[";
            static const uint8_t prefix_close[] = "] ";

            write_status = channel->write(channel->ctx, prefix_open, sizeof(prefix_open) - 1U);
            if (write_status >= 0) {
                write_status = channel->write(channel->ctx, (const uint8_t *)source_name, strlen(source_name));
            }
            if (write_status >= 0) {
                write_status = channel->write(channel->ctx, prefix_close, sizeof(prefix_close) - 1U);
            }
        }

        if (write_status >= 0) {
            write_status = channel->write(channel->ctx, data, len);
        }

        if (write_status >= 0) {
            static const uint8_t newline[] = "\r\n";
            (void)channel->write(channel->ctx, newline, sizeof(newline) - 1U);
            channel->tx_packets++;
            channel->tx_bytes += (uint32_t)len;
        } else {
            channel->tx_errors++;
            status = IOT_ROUTER_EIO;
        }
    }

    return status;
}

IotRouterStatus IotRouter_RoutePacket(IotRouter *router, uint32_t source, const uint8_t *data, size_t len)
{
    uint8_t matched = 0U;

    if ((router == NULL) || (source == 0U) || (data == NULL) || (len == 0U)) {
        return IOT_ROUTER_EINVAL;
    }

    router->stats.rx_packets++;
    router->stats.rx_bytes += (uint32_t)len;

    for (uint8_t i = 0U; i < router->rule_count; i++) {
        IotRouterRule *rule = &router->rules[i];

        if ((rule->flags & IOT_ROUTER_RULE_ENABLED) == 0U) {
            continue;
        }

        if ((rule->source_mask & source) == 0U) {
            continue;
        }

        if (router_contains(data, len, rule->contains, rule->flags) == 0) {
            continue;
        }

        matched = 1U;
        rule->match_count++;

        if (rule->action != NULL) {
            if (rule->action(router, rule, source, data, len, rule->action_ctx) != IOT_ROUTER_OK) {
                rule->drop_count++;
                router->stats.dropped_packets++;
            }
        }

        if (rule->output_mask != 0U) {
            if (IotRouter_Forward(router, rule->output_mask, source, data, len, rule->flags) == IOT_ROUTER_OK) {
                router->stats.routed_packets++;
            } else {
                rule->drop_count++;
                router->stats.dropped_packets++;
            }
        }

        if ((rule->flags & IOT_ROUTER_RULE_CONSUME_ON_MATCH) != 0U) {
            return IOT_ROUTER_OK;
        }
    }

    if (matched == 0U) {
        router->stats.dropped_packets++;
    }

    return IOT_ROUTER_OK;
}

const char *IotRouter_SourceName(uint32_t source)
{
    if ((source & IOT_ROUTER_SOURCE_UART) != 0U) {
        return "uart";
    }

    if ((source & IOT_ROUTER_SOURCE_UART2) != 0U) {
        return "uart2";
    }

    if ((source & IOT_ROUTER_SOURCE_USB) != 0U) {
        return "usb";
    }

    if ((source & IOT_ROUTER_SOURCE_APP) != 0U) {
        return "app";
    }

    return "unknown";
}

const IotRouterStats *IotRouter_GetStats(const IotRouter *router)
{
    if (router == NULL) {
        return NULL;
    }

    return &router->stats;
}
