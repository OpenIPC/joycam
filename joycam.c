/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
 *
 * joycam.c — Shared utilities: serial I/O, channel display, axis/button mapping
 *
 */

#include "joycam.h"
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <errno.h>

// --- Serial port helpers ---

static int serial_close_inner(crsf_handle_t* h) {
    if (h->fd >= 0) {
        tcdrain(h->fd);
        close(h->fd);
    }
    h->fd = -1;
    return 0;
}

int crsf_serial_open(const char* port_name, crsf_handle_t* h, int mode, int baudrate) {
    /* If URI starts with "tcp:", delegate to RFC 2217. */
    if (strncmp(port_name, "tcp:", 4) == 0) {
        char host[256];
        int port;
        if (parse_tcp_uri(port_name, host, sizeof(host), &port) < 0) {
            fprintf(stderr, "Error: invalid tcp URI '%s' (expected tcp:host:port)\n",
                    port_name);
            return -1;
        }
        /* Default parameters: 8N1, 1 stop bit */
        return rfc2217_open(h, host, port, baudrate, 8, 'N', 0);
    }

    int oflags = O_NOCTTY | O_NONBLOCK;
    if (mode == O_RDONLY)      oflags |= O_RDONLY;
    else if (mode == O_WRONLY) oflags |= O_WRONLY;
    else                       oflags |= O_RDWR;

    h->fd = open(port_name, oflags);
    if (h->fd < 0) {
        fprintf(stderr, "Error: cannot open %s: %s\n", port_name, strerror(errno));
        syslog(LOG_ERR, "failed to open %s: %s", port_name, strerror(errno));
        return -1;
    }

    h->type = TRANSPORT_SERIAL;

    /* Remove O_NONBLOCK after open — we use poll() for timed I/O. */
    int flags = fcntl(h->fd, F_GETFL);
    if (flags >= 0)
        fcntl(h->fd, F_SETFL, flags & ~O_NONBLOCK);

    /* Configure termios. */
    struct termios tio;
    if (tcgetattr(h->fd, &tio) == 0) {
        cfsetospeed(&tio, baudrate);
        cfsetispeed(&tio, baudrate);
        tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
        tio.c_cflag |= CS8 | CLOCAL | CREAD;
        tio.c_iflag  = IGNBRK;
        tio.c_oflag  = 0;
        tio.c_lflag  = 0;
        tcsetattr(h->fd, TCSANOW, &tio);
    }
    return 0;
}

void crsf_serial_close(crsf_handle_t* h) {
    if (h->fd < 0) return;
    if (h->type == TRANSPORT_RFC2217)
        rfc2217_close(h);
    else
        serial_close_inner(h);
}

int crsf_read(crsf_handle_t* h, void* buf, size_t len, int timeout_ms) {
    if (h->fd < 0) return -1;
    if (h->type == TRANSPORT_RFC2217)
        return rfc2217_read(h, buf, len, timeout_ms);
    struct pollfd pfd = { .fd = h->fd, .events = POLLIN };
    int pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0) return pr;
    return (int)read(h->fd, buf, len);
}

int crsf_write(crsf_handle_t* h, const void* buf, size_t len, int timeout_ms) {
    (void)timeout_ms;
    if (h->fd < 0) return -1;
    if (h->type == TRANSPORT_RFC2217)
        return rfc2217_write(h, buf, len);
    return (int)write(h->fd, buf, len);
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

/* Map evdev axis value to an arbitrary integer range using only int64_t. */
int axis_to_range(int value, int min, int max, int out_min, int out_max) {
    if (min == max) return (out_min + out_max) / 2;
    int64_t v = value - min;
    int64_t d = max - min;
    if (v < 0) v = 0;
    if (v > d) v = d;
    return (int)(out_min + (v * (out_max - out_min) + d / 2) / d);
}

/* Map evdev axis value to CRSF range (172..1811). */
int axis_to_crsf(int value, int min, int max) {
    return axis_to_range(value, min, max, CRSF_CHANNEL_MIN, CRSF_CHANNEL_MAX);
}

/* Map button code 304-315 (BTN_SOUTH..BTN_THUMBR) to channel offset. */
int button_to_channel(int code) {
    if (code >= 304 && code <= 315)
        return code - 304 + 8;  /* 304->ch8, 305->ch9, ..., 315->ch19 */
    return -1;  /* unmapped */
}
