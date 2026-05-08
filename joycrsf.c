/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  MIT License
 *
 * joycrsf.c — CRSF protocol parser, CRC8, packet generator
 *
 */

#include "joycrsf.h"
#include <string.h>
#include <syslog.h>

static crsf_packet_t rx_packet;
static uint8_t rx_index = 0;
static int have_sync = 0;

// --- Serial port helpers ---

int crsf_serial_open(const char* port_name, struct sp_port** port, int mode, int baudrate) {
    if (sp_get_port_by_name(port_name, port) != SP_OK) {
        fprintf(stderr, "Error: cannot get serial port %s\n", port_name);
        syslog(LOG_ERR, "failed to get serial port %s", port_name);
        return -1;
    }
    if (sp_open(*port, mode) != SP_OK) {
        fprintf(stderr, "Error: cannot open serial port %s\n", port_name);
        syslog(LOG_ERR, "failed to open serial port %s", port_name);
        sp_free_port(*port);
        *port = NULL;
        return -1;
    }
    if (sp_set_baudrate(*port, baudrate) != SP_OK) {
        fprintf(stderr, "Error: cannot set baudrate %d on %s\n", baudrate, port_name);
        syslog(LOG_ERR, "failed to set baudrate %d on %s", baudrate, port_name);
    }
    if (sp_set_parity(*port, SP_PARITY_NONE) != SP_OK)
        syslog(LOG_WARNING, "cannot set parity on %s", port_name);
    if (sp_set_bits(*port, 8) != SP_OK)
        syslog(LOG_WARNING, "cannot set 8 bits on %s", port_name);
    if (sp_set_stopbits(*port, 1) != SP_OK)
        syslog(LOG_WARNING, "cannot set 1 stop bit on %s", port_name);
    return 0;
}

void crsf_serial_close(struct sp_port* port) {
    if (port) {
        sp_close(port);
        sp_free_port(port);
    }
}

// --- Common utilities ---

void crsf_print_channels(const uint16_t* channels, int count) {
    char buf[256];
    int pos = 0;
    for (int i = 0; i < count && pos < (int)sizeof(buf) - 10; i++) {
        if (i > 0 && i % 8 == 0) {
            buf[pos++] = ' ';
            buf[pos++] = '|';
            buf[pos++] = ' ';
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%d:%-4d ", i, channels[i]);
    }
    buf[pos] = '\0';
    fputs(buf, stdout);
}

void crsf_hex_dump(const uint8_t* data, int len, const char* label) {
    printf("%s [%d] ", label ? label : "HEX", len);
    for (int i = 0; i < len; i++)
        printf("%02x ", data[i]);
    printf("\n");
}

// CRC8 with LUT (polynomial 0xD5)
static uint8_t crc8_table[256];
static int crc8_table_ready = 0;

static void crc8_init(void) {
    for (int i = 0; i < 256; i++) {
        uint8_t crc = i;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0xD5;
            else crc <<= 1;
        }
        crc8_table[i] = crc;
    }
    crc8_table_ready = 1;
}

uint8_t crsf_crc8(const uint8_t* data, uint16_t len) {
    if (!crc8_table_ready) crc8_init();
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++)
        crc = crc8_table[crc ^ data[i]];
    return crc;
}

int crsf_validate_packet(crsf_packet_t* packet) {
    if (packet->len < 2 || packet->len > CRSF_PAYLOAD_SIZE + 2)
        return 0;
    uint8_t calc_crc = crsf_crc8(&packet->type, packet->len - 1);
    return calc_crc == packet->crc ? 1 : 0;
}

int crsf_parse_byte(uint8_t data, crsf_channels_t* out_channels, crsf_link_stats_t* out_stats) {
    if (!out_channels)
        return -1;

    // Finite State Machine for packet parsing
    // Fast re-sync: if we see a sync byte while already parsing, restart
    if (have_sync && data == CRSF_SYNC_BYTE) {
        have_sync = 1;
        rx_index = 0;
        rx_packet.sync = data;
        return 0;
    }

    // State 1: wait for sync byte
    if (!have_sync) {
        if (data == CRSF_SYNC_BYTE) {
            have_sync = 1;
            rx_packet.sync = data;
        }
        return 0;
    }

    // State 2: read length byte
    if (rx_index == 0) {
        rx_packet.len = data;
        rx_index = 2;

        // Validate length: must be >= 3 (type + payload[0] + crc) and within bounds
        if (rx_packet.len < 3 || rx_packet.len > CRSF_PAYLOAD_SIZE + 2) {
            rx_index = 0;
            have_sync = 0;
            return -1; // Invalid frame length
        }
        return 0;
    }

    // State 3: read remaining bytes
    uint8_t* packet_ptr = (uint8_t*)&rx_packet;
    packet_ptr[rx_index++] = data;

    if (rx_index >= (rx_packet.len + 2)) {
        // Packet complete - validate CRC
        if (!crsf_validate_packet(&rx_packet)) {
            rx_index = 0;
            have_sync = 0;
            return -1; // CRC Error
        }

        // RC Channels Packet
        if (rx_packet.type == CRSF_FRAMETYPE_RC_CHANNELS_PACKED) {
            const uint8_t* payload = rx_packet.payload;
            for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
                uint32_t offset = (i * 11) / 8;
                uint8_t  shift  = (i * 11) % 8;
                uint32_t raw;
                /* Most channels span 3 bytes; channel 15 fits in 2. */
                if (offset + 2 < CRSF_PAYLOAD_SIZE)
                    raw = payload[offset] | (payload[offset + 1] << 8)
                                           | (payload[offset + 2] << 16);
                else
                    raw = payload[offset] | (payload[offset + 1] << 8);
                out_channels->channels[i] = (raw >> shift) & 0x7FF;
            }
            rx_index = 0;
            have_sync = 0;
            return 1;
        }
        // Link Statistics Packet
        if (rx_packet.type == CRSF_FRAMETYPE_LINK_STATISTICS && out_stats) {
            memcpy(out_stats, rx_packet.payload, sizeof(crsf_link_stats_t));
            rx_index = 0;
            have_sync = 0;
            return 2;
        }

        // Unknown packet type - skip but don't error
        rx_index = 0;
        have_sync = 0;
        return 0;
    }
    return 0;
}

void crsf_generate_rc_packet(uint8_t* buffer, const uint16_t* channels) {
    uint8_t payload[CRSF_PAYLOAD_SIZE] = {0};
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
        uint32_t offset = (i * 11) / 8;
        uint8_t  shift  = (i * 11) % 8;
        uint32_t bitmask = channels[i] & 0x7FF;
        payload[offset]     |= (bitmask << shift) & 0xFF;
        payload[offset + 1] |= (bitmask >> (8 - shift)) & 0xFF;
        /* Channel 15 needs only 2 bytes; guard against OOB write. */
        if (offset + 2 < CRSF_PAYLOAD_SIZE)
            payload[offset + 2] |= (bitmask >> (16 - shift)) & 0xFF;
    }

    buffer[0] = CRSF_SYNC_BYTE;
    buffer[1] = sizeof(payload) + 2; // type + payload + crc
    buffer[2] = CRSF_FRAMETYPE_RC_CHANNELS_PACKED;
    memcpy(&buffer[3], payload, sizeof(payload));
    buffer[3 + sizeof(payload)] = crsf_crc8(&buffer[2], sizeof(payload) + 1);
}
