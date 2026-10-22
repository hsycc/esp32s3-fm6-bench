#include "fm6_midi_router.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static bool valid_channel(uint8_t channel)
{
    return channel <= 15u || channel == FM6_MIDI_CHANNEL_OMNI;
}

static uint8_t expected_data_bytes(uint8_t status)
{
    switch (status & 0xf0u) {
    case 0x80u:
    case 0x90u:
    case 0xa0u:
    case 0xb0u:
    case 0xe0u:
        return 2u;
    case 0xc0u:
    case 0xd0u:
        return 1u;
    default:
        return 0u;
    }
}

bool fm6_midi_router_init(fm6_midi_router_t *router,
                          fm6_control_queue_t *queue,
                          const fm6_program_bank_t *bank,
                          uint8_t target_channel,
                          unsigned voice_capacity)
{
    if (router == NULL || queue == NULL || bank == NULL ||
        !valid_channel(target_channel) ||
        voice_capacity == 0u || voice_capacity > FM6_BENCH_MAX_VOICES) {
        return false;
    }

    memset(router, 0, sizeof(*router));
    router->queue = queue;
    router->bank = bank;
    router->target_channel = target_channel;
    router->voice_capacity = (uint8_t)voice_capacity;
    return true;
}

bool fm6_midi_router_set_channel(fm6_midi_router_t *router, uint8_t target_channel)
{
    if (router == NULL || !valid_channel(target_channel)) {
        return false;
    }
    router->target_channel = target_channel;
    return true;
}

void fm6_midi_router_reset_parser(fm6_midi_router_t *router)
{
    if (router == NULL) {
        return;
    }
    router->running_status = 0u;
    router->data_count = 0u;
    router->expected_data = 0u;
    router->in_sysex = false;
}

static bool route_submit(fm6_midi_router_t *router, bool accepted)
{
    if (accepted) {
        ++router->stats.messages_routed;
    } else {
        ++router->stats.queue_rejected;
    }
    return accepted;
}

static void dispatch_message(fm6_midi_router_t *router,
                             uint8_t status,
                             const uint8_t *data)
{
    ++router->stats.messages_seen;

    const uint8_t channel = status & 0x0fu;
    if (router->target_channel != FM6_MIDI_CHANNEL_OMNI &&
        channel != router->target_channel) {
        ++router->stats.channel_ignored;
        return;
    }

    switch (status & 0xf0u) {
    case 0x80u:
        (void)route_submit(
            router,
            fm6_control_queue_submit_note_off(router->queue, data[0], NULL));
        break;

    case 0x90u:
        (void)route_submit(
            router,
            fm6_control_queue_submit_note_on(router->queue, data[0], data[1], NULL));
        break;

    case 0xb0u:
        switch (data[0]) {
        case 64u:
            (void)route_submit(
                router,
                fm6_control_queue_submit_sustain(router->queue, data[1] >= 64u, NULL));
            break;
        case 120u:
            (void)route_submit(
                router,
                fm6_control_queue_submit_all_sound_off(router->queue, NULL));
            break;
        case 123u:
            (void)route_submit(
                router,
                fm6_control_queue_submit_all_notes_off(router->queue, NULL));
            break;
        default:
            ++router->stats.unsupported_messages;
            break;
        }
        break;

    case 0xc0u: {
        const uint8_t program = data[0];
        const uint8_t *voice = fm6_program_bank_get(router->bank, program);
        if (voice == NULL) {
            ++router->stats.missing_program;
            break;
        }
        if (route_submit(
                router,
                fm6_control_queue_submit_program(
                    router->queue,
                    voice,
                    router->voice_capacity,
                    NULL))) {
            router->last_program = program;
            router->have_program = true;
        }
        break;
    }

    case 0xa0u:
    case 0xd0u:
    case 0xe0u:
    default:
        ++router->stats.unsupported_messages;
        break;
    }
}

void fm6_midi_router_feed_byte(fm6_midi_router_t *router, uint8_t byte)
{
    if (router == NULL) {
        return;
    }
    ++router->stats.bytes_seen;

    /* MIDI real-time messages may appear between any bytes, including inside
     * running-status messages and SysEx. They never cancel running status. */
    if (byte >= 0xf8u) {
        ++router->stats.realtime_bytes;
        return;
    }

    if (router->in_sysex) {
        ++router->stats.sysex_bytes;
        if (byte == 0xf7u) {
            router->in_sysex = false;
        }
        return;
    }

    if ((byte & 0x80u) != 0u) {
        router->data_count = 0u;

        if (byte == 0xf0u) {
            router->running_status = 0u;
            router->expected_data = 0u;
            router->in_sysex = true;
            ++router->stats.sysex_bytes;
            return;
        }

        if (byte >= 0xf1u) {
            /* System-common statuses cancel channel running status. This router
             * deliberately leaves their payload to the transport layer. */
            router->running_status = 0u;
            router->expected_data = 0u;
            ++router->stats.unsupported_messages;
            return;
        }

        router->running_status = byte;
        router->expected_data = expected_data_bytes(byte);
        if (router->expected_data == 0u) {
            router->running_status = 0u;
        }
        return;
    }

    if (router->running_status == 0u || router->expected_data == 0u) {
        ++router->stats.orphan_data;
        return;
    }

    router->data[router->data_count++] = byte;
    if (router->data_count >= router->expected_data) {
        dispatch_message(router, router->running_status, router->data);
        router->data_count = 0u;
    }
}

void fm6_midi_router_feed(fm6_midi_router_t *router, const uint8_t *data, size_t size)
{
    if (router == NULL || (data == NULL && size != 0u)) {
        return;
    }
    for (size_t i = 0; i < size; ++i) {
        fm6_midi_router_feed_byte(router, data[i]);
    }
}

const fm6_midi_router_stats_t *fm6_midi_router_stats(const fm6_midi_router_t *router)
{
    return router == NULL ? NULL : &router->stats;
}

bool fm6_midi_router_last_program(const fm6_midi_router_t *router, uint8_t *program_out)
{
    if (router == NULL || program_out == NULL || !router->have_program) {
        return false;
    }
    *program_out = router->last_program;
    return true;
}
