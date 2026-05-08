/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  MIT License
 *
 * crsf_driver.c — CRSF protocol parser, CRC8, packet generator
 *
 */

#include "crsf_driver.h"
#include <string.h>

static crsf_packet_t rx_packet;
static uint8_t rx_index = 0;
static _Bool have_sync = 0;

// CRC8 calculation polynomial 0xD5
uint8_t crsf_crc8(const uint8_t* data, uint16_t len) {
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0xD5;
            else crc = (crc << 1);
        }
    }
    return crc;
}

_Bool crsf_validate_packet(crsf_packet_t* packet) {
    uint8_t calc_crc = crsf_crc8(&packet->type, packet->len - 1);
    return calc_crc == packet->crc;
}

int crsf_parse_byte(uint8_t data, crsf_channels_t* out_channels, crsf_link_stats_t* out_stats) {
    // Finite State Machine for packet parsing
    if (!have_sync && data == CRSF_SYNC_BYTE) {
        have_sync = 1;
        rx_packet.sync = data;
        return 0;
    }

    if (have_sync) {
        rx_packet.len = data;
        rx_index = 2;
        have_sync = 0;
        return 0;
    }

    uint8_t* packet_ptr = (uint8_t*)&rx_packet;
    packet_ptr[rx_index++] = data;

    if (rx_index >= (rx_packet.len + 2)) {
        if (!crsf_validate_packet(&rx_packet)) {
            rx_index = 0;
            return -1; // CRC Error
        }

        // RC Channels Packet
        if (rx_packet.type == CRSF_FRAMETYPE_RC_CHANNELS_PACKED) {
            const uint8_t* payload = rx_packet.payload;
            for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
                out_channels->channels[i] = 0;
                if ((i * 11) / 8 < 21) {
                    uint32_t offset = (i * 11) / 8;
                    out_channels->channels[i] = (payload[offset] |
                                                 (payload[offset + 1] << 8) |
                                                 (payload[offset + 2] << 16)) &
                                                ((1 << 11) - 1);
                }
            }
            rx_index = 0;
            return 1;
        }
        // Link Statistics Packet
        else if (rx_packet.type == CRSF_FRAMETYPE_LINK_STATISTICS && out_stats) {
            memcpy(out_stats, rx_packet.payload, sizeof(crsf_link_stats_t));
            rx_index = 0;
            return 2;
        }

        rx_index = 0;
        return 0;
    }
    return 0;
}

void crsf_generate_rc_packet(uint8_t* buffer, const uint16_t* channels) {
    uint8_t payload[22] = {0};
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
        uint32_t offset = (i * 11) / 8;
        uint32_t bitmask = channels[i] & ((1 << 11) - 1);
        payload[offset] |= (bitmask << ((i * 11) % 8)) & 0xFF;
        payload[offset + 1] |= (bitmask >> (8 - ((i * 11) % 8))) & 0xFF;
        payload[offset + 2] |= (bitmask >> (16 - ((i * 11) % 8))) & 0xFF;
    }

    buffer[0] = CRSF_SYNC_BYTE;
    buffer[1] = sizeof(payload) + 2; // type + payload + crc
    buffer[2] = CRSF_FRAMETYPE_RC_CHANNELS_PACKED;
    memcpy(&buffer[3], payload, sizeof(payload));
    buffer[3 + sizeof(payload)] = crsf_crc8(&buffer[2], sizeof(payload) + 1);
}
