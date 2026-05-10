/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
 *
 * joycam.h — CRSF and IBUS protocol definitions, constants, structures
 *
 */

#ifndef JOYCAM_H
#define JOYCAM_H

#include <stdint.h>
#include <stdio.h>
#include <termios.h>

// --- CRSF Protocol Constants ---
#define CRSF_SYNC_BYTE 0xC8
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16
#define CRSF_FRAMETYPE_LINK_STATISTICS 0x14
#define CRSF_NUM_CHANNELS 16
#define CRSF_PAYLOAD_SIZE 22
/* sync(1) + len(1) + type(1) + payload(CRSF_PAYLOAD_SIZE) + crc(1) */
#define CRSF_TOTAL_FRAME_SIZE (3 + CRSF_PAYLOAD_SIZE + 1)
#define CRSF_BAUDRATE 420000
#define CRSF_CHANNEL_MIN 172
#define CRSF_CHANNEL_MAX 1811
#define CRSF_CHANNEL_MID 992

// --- SBUS Protocol Constants ---
#define SBUS_START_BYTE 0x0F
#define SBUS_END_BYTE   0x00
#define SBUS_NUM_CHANNELS 16
#define SBUS_PACKET_SIZE 25
/* start(1) + channels(22) + flags(1) + end(1) */
#define SBUS_BAUDRATE 100000
#define SBUS_CHANNEL_MIN 172
#define SBUS_CHANNEL_MAX 1811
#define SBUS_CHANNEL_MID 992

// --- IBUS Protocol Constants ---
#define IBUS_SYNC1 0x20
#define IBUS_SYNC2 0x40
#define IBUS_NUM_CHANNELS 14
#define IBUS_PACKET_SIZE 32
/* header(2) + channels(28) + checksum(2) */
#define IBUS_BAUDRATE 115200
#define IBUS_CHANNEL_MIN 1000
#define IBUS_CHANNEL_MAX 2000
#define IBUS_CHANNEL_MID 1500

// --- Packet Structures ---
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

typedef struct {
    uint16_t channels[IBUS_NUM_CHANNELS];
} ibus_channels_t;

#pragma pack(pop)

// --- Transport types ---
#define TRANSPORT_SERIAL   0
#define TRANSPORT_RFC2217  1

// --- RFC 2217 state (stored inside crsf_handle_t) ---
typedef struct {
    uint8_t buf[512];         /* de-escaped application data buffer */
    int     buf_len;          /* bytes currently in buf */
    uint8_t sub_buf[128];     /* sub-negotiation payload buffer */
    int     sub_len;          /* bytes in sub_buf */
    uint8_t iac_state;        /* IAC state machine state */
    uint8_t iac_cmd;          /* pending WILL/WONT/DO/DONT */
} rfc2217_state_t;

// --- Serial port / RFC 2217 handle ---
typedef struct {
    int  fd;                   /* POSIX fd, -1 if closed */
    int  type;                 /* TRANSPORT_SERIAL or TRANSPORT_RFC2217 */
    union {
        rfc2217_state_t tcp;
    } u;
} crsf_handle_t;

int  set_baudrate_custom(int fd, int speed);
int  crsf_serial_open(const char* port_name, crsf_handle_t* h, int mode, int baudrate);
void crsf_serial_close(crsf_handle_t* h);
int  crsf_read(crsf_handle_t* h, void* buf, size_t len, int timeout_ms);
int  crsf_write(crsf_handle_t* h, const void* buf, size_t len, int timeout_ms);

// --- RFC 2217 functions ---
int  parse_tcp_uri(const char* uri, char* host, int hostlen, int* port);
int  rfc2217_open(crsf_handle_t* h, const char* host, int port,
                   int baudrate, int datasize, char parity, int stopbits);
int  rfc2217_read(crsf_handle_t* h, void* buf, size_t len, int timeout_ms);
int  rfc2217_write(crsf_handle_t* h, const void* buf, size_t len);
void rfc2217_close(crsf_handle_t* h);

// --- Common utilities ---
void crsf_print_channels(const uint16_t* channels, int count);
void crsf_hex_dump(const uint8_t* data, int len, const char* label);
int  axis_to_crsf(int value, int min, int max);
int  axis_to_range(int value, int min, int max, int out_min, int out_max);
int  button_to_channel(int code);

// --- CRSF protocol functions ---
int     crsf_validate_packet(crsf_packet_t* packet);
int     crsf_parse_byte(uint8_t data, crsf_channels_t* out_channels,
                        crsf_link_stats_t* out_stats);
void    crsf_generate_rc_packet(uint8_t* buffer, const uint16_t* channels);
uint8_t crsf_crc8(const uint8_t* data, uint16_t len);

// --- SBUS protocol functions ---
void sbus_generate_packet(uint8_t* buffer, const uint16_t* channels);
int  sbus_parse_byte(uint8_t data, crsf_channels_t* out_channels);

// --- IBUS protocol functions ---
uint16_t ibus_checksum(const uint8_t* data, int len);
void     ibus_generate_packet(uint8_t* buffer, const uint16_t* channels);
int      ibus_parse_byte(uint8_t data, ibus_channels_t* out_channels);

#endif // JOYCAM_H
