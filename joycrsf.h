/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  MIT License
 *
 * joycrsf.h — CRSF protocol definitions, constants, structures
 *
 */

#ifndef JOYCRSF_H
#define JOYCRSF_H

#include <stdint.h>
#include <stdio.h>
#include <libserialport.h>

// --- CRSF Protocol Constants ---
#define CRSF_SYNC_BYTE 0xC8
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16
#define CRSF_FRAMETYPE_LINK_STATISTICS 0x14
#define CRSF_NUM_CHANNELS 16
#define CRSF_PAYLOAD_SIZE 22
/* sync(1) + len(1) + type(1) + payload(CRSF_PAYLOAD_SIZE) + crc(1) */
#define CRSF_TOTAL_FRAME_SIZE (3 + CRSF_PAYLOAD_SIZE + 1)

// --- Packet Structure ---
#pragma pack(push, 1)
typedef struct {
    uint8_t sync;   // 0xC8
    uint8_t len;    // total length = type + payload + crc
    uint8_t type;   // e.g., 0x16 for RC channels
    uint8_t payload[CRSF_PAYLOAD_SIZE];
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

// --- Serial port helpers ---
int  crsf_serial_open(const char* port_name, struct sp_port** port, int mode, int baudrate);
void crsf_serial_close(struct sp_port* port);

// --- Common utilities ---
void crsf_print_channels(const uint16_t* channels, int count);
void crsf_hex_dump(const uint8_t* data, int len, const char* label);

// --- Protocol functions ---
int crsf_validate_packet(crsf_packet_t* packet);
int crsf_parse_byte(uint8_t data, crsf_channels_t* out_channels, crsf_link_stats_t* out_stats);
void crsf_generate_rc_packet(uint8_t* buffer, const uint16_t* channels);
uint8_t crsf_crc8(const uint8_t* data, uint16_t len);

#endif // JOYCRSF_H