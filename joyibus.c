/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
 *
 * joyibus.c — IBUS (FlySky) protocol: checksum, packet generator, parser FSM
 *
 */

#include "joycam.h"
#include <string.h>

static uint8_t ibus_pkt[IBUS_PACKET_SIZE];
static int ibus_index = 0;
static int ibus_have_sync = 0;

/*
 * Compute IBUS checksum: 0xFFFF - sum of all bytes.
 * FlySky IBUS stores the 16-bit checksum in the last two bytes (little-endian).
 */
uint16_t ibus_checksum(const uint8_t* data, int len) {
    uint16_t sum = 0;
    for (int i = 0; i < len; i++)
        sum += data[i];
    return 0xFFFF - sum;
}

/*
 * Generate a 32-byte IBUS RC packet from 14 channels (1000-2000 range).
 *
 * Layout:
 *   [0]     = 0x20 (IBUS_SYNC1)
 *   [1]     = 0x40 (IBUS_SYNC2)
 *   [2..29] = 14 channels, each 16-bit little-endian
 *   [30]    = checksum low byte
 *   [31]    = checksum high byte
 */
void ibus_generate_packet(uint8_t* buffer, const uint16_t* channels) {
    buffer[0] = IBUS_SYNC1;
    buffer[1] = IBUS_SYNC2;

    for (int i = 0; i < IBUS_NUM_CHANNELS; i++) {
        buffer[2 + i * 2]     =  channels[i]       & 0xFF;
        buffer[2 + i * 2 + 1] = (channels[i] >> 8) & 0xFF;
    }

    uint16_t csum = ibus_checksum(buffer, 30);
    buffer[30] = csum       & 0xFF;
    buffer[31] = (csum >> 8) & 0xFF;
}

/*
 * Finite State Machine for IBUS byte-by-byte parsing.
 *
 * Returns:
 *   1  — complete and valid 14-channel packet (out_channels filled)
 *   0  — parsing in progress, need more bytes
 *  -1  — checksum error
 */
int ibus_parse_byte(uint8_t data, ibus_channels_t* out_channels) {
    if (!out_channels)
        return -1;

    /* Wait for first sync byte. */
    if (!ibus_have_sync) {
        if (data == IBUS_SYNC1) {
            ibus_have_sync = 1;
            ibus_pkt[0] = data;
            ibus_index = 1;
        }
        return 0;
    }

    /* Expect second sync byte. */
    if (ibus_index == 1) {
        if (data == IBUS_SYNC2) {
            ibus_pkt[1] = data;
            ibus_index++;
        } else {
            ibus_have_sync = 0;  /* False start — reset. */
        }
        return 0;
    }

    /* Read remaining bytes into buffer. */
    ibus_pkt[ibus_index++] = data;

    if (ibus_index >= IBUS_PACKET_SIZE) {
        ibus_have_sync = 0;
        ibus_index = 0;

        /* Validate checksum over first 30 bytes. */
        uint16_t csum     = ibus_checksum(ibus_pkt, 30);
        uint16_t pkt_csum = ibus_pkt[30] | ((uint16_t)ibus_pkt[31] << 8);
        if (csum != pkt_csum)
            return -1;  /* Checksum error */

        /* Extract 14 channels (LE 16-bit at offset 2..29). */
        for (int i = 0; i < IBUS_NUM_CHANNELS; i++) {
            out_channels->channels[i] = ibus_pkt[2 + i * 2]
                                        | ((uint16_t)ibus_pkt[2 + i * 2 + 1] << 8);
        }
        return 1;
    }
    return 0;
}
