/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
 *
 * joysbus.c — SBUS (Futaba) protocol: packet generator, parser FSM
 *
 * SBUS is 100000 baud, 8E2 (even parity, 2 stop bits), inverted signal.
 * On Linux, the inverted signal is handled by most SoC UART drivers
 * automatically; external USB-UART adapters may need a hardware inverter.
 *
 * Custom baud rate (100000) uses termios2 via ioctl(TCGETS2/TCSETS2).
 * We define struct termios2 and ioctl numbers directly to avoid
 * conflicts between <asm/termbits.h> and glibc's <termios.h>.
 *
 */

#include "joycam.h"
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

/* --- termios2 helpers (custom baud rates) --- */

/*
 * struct termios2 / termios2 ioctls live in <asm/termbits.h> and
 * <asm-generic/ioctls.h>, but including those alongside glibc's
 * <termios.h> causes a redefinition of struct termios.
 *
 * We define just what we need here to stay portable across glibc,
 * musl and uClibc toolchains.
 */
#ifndef BOTHER
#define BOTHER 0010000
#endif

struct termios2 {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[19];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

#ifndef TCGETS2
#define TCGETS2  _IOR('T', 0x2A, struct termios2)
#endif
#ifndef TCSETS2
#define TCSETS2  _IOW('T', 0x2B, struct termios2)
#endif

static uint8_t sbus_pkt[SBUS_PACKET_SIZE];
static int sbus_index = 0;
static int sbus_have_sync = 0;

/*
 * Set a custom (non-standard) baud rate on an open serial port.
 * Uses termios2 (BOTHER) — the standard termios API only supports
 * rates defined in <termios.h>.
 */
int set_baudrate_custom(int fd, int speed) {
    struct termios2 tio;
    if (ioctl(fd, TCGETS2, &tio) < 0) {
        /* Termios2 not available on this kernel. */
        syslog(LOG_ERR, "termios2 not available for baudrate %d", speed);
        return -1;
    }
    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = speed;
    tio.c_ospeed = speed;
    if (ioctl(fd, TCSETS2, &tio) < 0) {
        syslog(LOG_ERR, "failed to set custom baudrate %d", speed);
        return -1;
    }
    return 0;
}

/*
 * Generate a 25-byte SBUS frame from 16 channels (172-1811 range, 11-bit).
 *
 * Layout:
 *   [0]        = 0x0F (SBUS_START_BYTE)
 *   [1..22]    = 16 channels × 11 bits (same packing as CRSF)
 *   [23]       = flags (bit7=ch17, bit6=ch18, bit5=lost_frame, bit4=failsafe)
 *   [24]       = 0x00 (SBUS_END_BYTE)
 */
void sbus_generate_packet(uint8_t* buffer, const uint16_t* channels) {
    /* Pack 16 x 11-bit channels into 22 bytes (same logic as CRSF). */
    uint8_t payload[22] = {0};
    for (int i = 0; i < SBUS_NUM_CHANNELS; i++) {
        uint32_t offset = (i * 11) / 8;
        uint8_t  shift  = (i * 11) % 8;
        uint32_t bitmask = channels[i] & 0x7FF;
        payload[offset]     |= (bitmask << shift) & 0xFF;
        payload[offset + 1] |= (bitmask >> (8 - shift)) & 0xFF;
        if (offset + 2 < 22)
            payload[offset + 2] |= (bitmask >> (16 - shift)) & 0xFF;
    }

    buffer[0] = SBUS_START_BYTE;
    memcpy(&buffer[1], payload, 22);
    buffer[23] = 0;  /* flags: no ch17/ch18, no failsafe */
    buffer[24] = SBUS_END_BYTE;
}

/*
 * Finite State Machine for SBUS byte-by-byte parsing.
 *
 * Returns:
 *   1  — complete and valid 16-channel packet (out_channels filled)
 *   0  — parsing in progress, need more bytes
 *  -1  — invalid frame
 */
int sbus_parse_byte(uint8_t data, crsf_channels_t* out_channels) {
    if (!out_channels)
        return -1;

    /* Wait for start byte. */
    if (!sbus_have_sync) {
        if (data == SBUS_START_BYTE) {
            sbus_have_sync = 1;
            sbus_pkt[0] = data;
            sbus_index = 1;
        }
        return 0;
    }

    sbus_pkt[sbus_index++] = data;

    if (sbus_index >= SBUS_PACKET_SIZE) {
        sbus_have_sync = 0;
        sbus_index = 0;

        /* Validate end byte. */
        if (sbus_pkt[SBUS_PACKET_SIZE - 1] != SBUS_END_BYTE)
            return -1;

        /* Extract 16 channels (11-bit, same packing as CRSF). */
        const uint8_t* payload = sbus_pkt + 1;  /* skip start byte */
        for (int i = 0; i < SBUS_NUM_CHANNELS; i++) {
            uint32_t offset = (i * 11) / 8;
            uint8_t  shift  = (i * 11) % 8;
            uint32_t raw;
            if (offset + 2 < 22)  /* same as CRSF_PAYLOAD_SIZE */
                raw = payload[offset] | (payload[offset + 1] << 8)
                                       | (payload[offset + 2] << 16);
            else
                raw = payload[offset] | (payload[offset + 1] << 8);
            out_channels->channels[i] = (raw >> shift) & 0x7FF;
        }

        /* flags byte at index 23: bit5=lost_frame, bit4=failsafe */
        return 1;
    }
    return 0;
}
