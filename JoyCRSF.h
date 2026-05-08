/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  MIT License
 *
 * JoyCRSF.h — CRSF protocol definitions, constants, structures
 *
 */

#ifndef JOYCRSF_H
#define JOYCRSF_H

#include <stdint.h>

// --- CRSF Protocol Constants ---
#define CRSF_SYNC_BYTE 0xC8
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16
#define CRSF_FRAMETYPE_LINK_STATISTICS 0x14
#define CRSF_NUM_CHANNELS 16

// --- Packet Structure ---
#pragma pack(push, 1)
typedef struct {
    uint8_t sync;   // 0xC8
    uint8_t len;    // total length = type + payload + crc
    uint8_t type;   // e.g., 0x16 for RC channels
    uint8_t payload[22];
    uint8_t crc;
} crsf_packet_t;

typedef struct {
    uint16_t channels[CRSF_NUM_CHANNELS];
} crsf_channels_t;

typedef struct {
    uint8_t uptime[4];
    uint8_t rssi_1;
    uint8_t rssi_2;
    uint8_t antenna;
    uint8_t rf_power;
    uint8_t quality;
} crsf_link_stats_t;
#pragma pack(pop)

// --- Project version ---
#define CRSF_BRIDGE_VERSION  "1.0.0"

// --- Functions ---
_Bool crsf_validate_packet(crsf_packet_t* packet);
int crsf_parse_byte(uint8_t data, crsf_channels_t* out_channels, crsf_link_stats_t* out_stats);
void crsf_generate_rc_packet(uint8_t* buffer, const uint16_t* channels);
uint8_t crsf_crc8(const uint8_t* data, uint16_t len);

#endif // JOYCRSF_H