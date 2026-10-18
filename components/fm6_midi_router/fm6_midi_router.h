#pragma once

#include <stddef.h>
#include <stdint.h>

#include "fm6_control_runtime.h"
#include "fm6_program_bank.h"

#define FM6_MIDI_CHANNEL_OMNI 0xffu

typedef struct {
    uint32_t bytes_seen;
    uint32_t messages_seen;
    uint32_t messages_routed;
    uint32_t channel_ignored;
    uint32_t unsupported_messages;
    uint32_t realtime_bytes;
    uint32_t sysex_bytes;
    uint32_t orphan_data;
    uint32_t missing_program;
    uint32_t queue_rejected;
} fm6_midi_router_stats_t;

typedef struct {
    fm6_control_queue_t *queue;
    const fm6_program_bank_t *bank;
    uint8_t target_channel;
    uint8_t voice_capacity;
    uint8_t running_status;
    uint8_t data[2];
    uint8_t data_count;
    uint8_t expected_data;
    bool in_sysex;
    uint8_t last_program;
    bool have_program;
    fm6_midi_router_stats_t stats;
} fm6_midi_router_t;

bool fm6_midi_router_init(fm6_midi_router_t *router,
                          fm6_control_queue_t *queue,
                          const fm6_program_bank_t *bank,
                          uint8_t target_channel,
                          unsigned voice_capacity);
bool fm6_midi_router_set_channel(fm6_midi_router_t *router, uint8_t target_channel);
void fm6_midi_router_reset_parser(fm6_midi_router_t *router);
void fm6_midi_router_feed_byte(fm6_midi_router_t *router, uint8_t byte);
void fm6_midi_router_feed(fm6_midi_router_t *router, const uint8_t *data, size_t size);
const fm6_midi_router_stats_t *fm6_midi_router_stats(const fm6_midi_router_t *router);
bool fm6_midi_router_last_program(const fm6_midi_router_t *router, uint8_t *program_out);
