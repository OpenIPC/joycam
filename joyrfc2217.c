/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  MIT License
 *
 * joyrfc2217.c — RFC 2217 (Telnet COM Port Control Option) implementation
 *
 * Provides TCP client with Telnet IAC negotiation, COM-PORT-OPTION
 * subcommands, and 0xFF byte escaping/unescaping for transparent
 * serial-over-TCP data transfer.
 *
 */

#include "joycam.h"
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>

/* --- Telnet / RFC 2217 constants --- */

#define TELNET_IAC      0xFF
#define TELNET_WILL     0xFB
#define TELNET_WONT     0xFC
#define TELNET_DO       0xFD
#define TELNET_DONT     0xFE
#define TELNET_SB       0xFA
#define TELNET_SE       0xF0
#define TELNET_NOP      0xF1

#define RFC2217_OPTION  0x2C   /* COM-PORT-OPTION (44) */

/* RFC 2217 sub-commands */
#define RFC2217_SET_BAUDRATE       1
#define RFC2217_SET_DATASIZE       2
#define RFC2217_SET_PARITY         3
#define RFC2217_SET_STOPBITS       4
#define RFC2217_SET_CONTROL        5
#define RFC2217_NOTIFY_LINESTATE   6
#define RFC2217_NOTIFY_MODEMSTATE  7
#define RFC2217_SET_LINESTATE_MASK 10
#define RFC2217_SET_MODEMSTATE_MASK 11
#define RFC2217_PURGE_DATA         12

/* --- IAC parser states --- */
#define IAC_IDLE        0
#define IAC_GOT_CMD     1   /* got IAC, reading WILL/WONT/DO/DONT/SB */
#define IAC_GOT_OPT     2   /* reading option byte after WILL/WONT/DO/DONT */
#define IAC_SUB         3   /* inside sub-negotiation (SB ... IAC SE) */
#define IAC_ESC         4   /* escaped 0xFF within sub-negotiation data */

/* --- Local state per handle (stored in crsf_handle_t.u.tcp) --- */

/*
 * Parse a "tcp:host:port" URI.
 * Returns 0 on success, -1 on error.
 */
int parse_tcp_uri(const char* uri, char* host, int hostlen, int* port) {
    if (strncmp(uri, "tcp:", 4) != 0)
        return -1;

    const char* p = uri + 4;
    const char* colon = strrchr(p, ':');
    if (!colon || colon == p)
        return -1;

    size_t hlen = colon - p;
    if ((int)hlen >= hostlen)
        return -1;
    memcpy(host, p, hlen);
    host[hlen] = '\0';

    char* end = NULL;
    long pval = strtol(colon + 1, &end, 10);
    if (end == colon + 1 || *end != '\0' || pval < 1 || pval > 65535)
        return -1;
    *port = (int)pval;
    return 0;
}

/*
 * Send a Telnet command (3 bytes: IAC <cmd> <opt>).
 */
static int send_telnet_cmd(crsf_handle_t* h, uint8_t cmd, uint8_t opt) {
    uint8_t buf[3] = { TELNET_IAC, cmd, opt };
    return (int)send(h->fd, buf, 3, 0);
}

/*
 * Send a RFC 2217 sub-negotiation:
 *   IAC SB COM-PORT-OPTION <subcmd> <data...> IAC SE
 */
static int send_rfc2217_sub(crsf_handle_t* h, uint8_t subcmd,
                            const uint8_t* data, int datalen) {
    uint8_t hdr[4] = { TELNET_IAC, TELNET_SB, RFC2217_OPTION, subcmd };
    uint8_t tlr[2] = { TELNET_IAC, TELNET_SE };
    struct iovec iov[3];
    int niov = 0;

    iov[niov].iov_base = hdr;
    iov[niov].iov_len  = sizeof(hdr);
    niov++;

    if (data && datalen > 0) {
        iov[niov].iov_base = (void*)data;
        iov[niov].iov_len  = datalen;
        niov++;
    }

    iov[niov].iov_base = tlr;
    iov[niov].iov_len  = sizeof(tlr);
    niov++;

    ssize_t total = 0;
    for (int i = 0; i < niov; i++) {
        ssize_t n = send(h->fd, iov[i].iov_base, iov[i].iov_len, 0);
        if (n < 0) return -1;
        total += n;
    }
    return (int)total;
}

/*
 * Send a 32-bit big-endian baud rate sub-negotiation.
 */
static int send_baudrate(crsf_handle_t* h, int baudrate) {
    uint8_t data[4];
    data[0] = (baudrate >> 24) & 0xFF;
    data[1] = (baudrate >> 16) & 0xFF;
    data[2] = (baudrate >> 8) & 0xFF;
    data[3] = baudrate & 0xFF;
    return send_rfc2217_sub(h, RFC2217_SET_BAUDRATE, data, 4);
}

/*
 * Perform RFC 2217 Telnet negotiation.
 * We offer WILL/WONT COM-PORT-OPTION, negotiate baudrate/datasize/parity/stopbits.
 * Returns 0 on success, -1 on error.
 */
static int rfc2217_negotiate(crsf_handle_t* h, int baudrate,
                              int datasize, char parity, int stopbits) {
    /* Phase 1: offer our capability */
    if (send_telnet_cmd(h, TELNET_WILL, RFC2217_OPTION) < 0)
        return -1;

    /* Wait for DO/DONT response with short timeout */
    struct pollfd pfd = { .fd = h->fd, .events = POLLIN };
    uint8_t buf[32];
    int attempts = 0;
    int got_response = 0;

    while (attempts < 10) {
        int pr = poll(&pfd, 1, 50);
        if (pr < 0) return -1;
        if (pr == 0) { attempts++; continue; }

        ssize_t n = recv(h->fd, buf, sizeof(buf), 0);
        if (n <= 0) return -1;

        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == TELNET_IAC) {
                if (i + 2 >= n) continue;
                uint8_t cmd = buf[i + 1];
                uint8_t opt = buf[i + 2];
                if (cmd == TELNET_DO && opt == RFC2217_OPTION) {
                    got_response = 1;
                    /* Acknowledge */
                    if (send_telnet_cmd(h, TELNET_WILL, RFC2217_OPTION) < 0)
                        return -1;
                }
                i += 2;
            }
        }
        if (got_response) break;
        attempts++;
    }

    if (!got_response) {
        syslog(LOG_WARNING, "RFC 2217: peer did not acknowledge COM-PORT-OPTION");
        /* Continue anyway — some peers work without negotiation */
    }

    /* Phase 2: configure port parameters */
    if (send_baudrate(h, baudrate) < 0)
        return -1;

    /* datasize (5-8) */
    {   uint8_t ds = (uint8_t)datasize;
        send_rfc2217_sub(h, RFC2217_SET_DATASIZE, &ds, 1);
    }
    /* parity: 0=none, 1=odd, 2=even, 3=mark, 4=space */
    {   uint8_t p = 0;
        switch (parity) {
            case 'N': case 'n': p = 0; break;
            case 'O': case 'o': p = 1; break;
            case 'E': case 'e': p = 2; break;
            case 'M': case 'm': p = 3; break;
            case 'S': case 's': p = 4; break;
            default:            p = 0; break;
        }
        send_rfc2217_sub(h, RFC2217_SET_PARITY, &p, 1);
    }
    /* stopbits: 0=1 stop, 1=2 stop, 2=1.5 stop */
    {   uint8_t sb = (uint8_t)stopbits;
        send_rfc2217_sub(h, RFC2217_SET_STOPBITS, &sb, 1);
    }

    /* Phase 3: request line-state and modem-state notifications */
    {
        uint8_t mask = 0x01; /* monitor DSR changes */
        send_rfc2217_sub(h, RFC2217_SET_LINESTATE_MASK, &mask, 1);
        send_rfc2217_sub(h, RFC2217_SET_MODEMSTATE_MASK, &mask, 1);
    }

    syslog(LOG_INFO, "RFC 2217 negotiation complete on fd=%d baud=%d",
           h->fd, baudrate);
    return 0;
}

/*
 * Open a TCP connection and perform RFC 2217 negotiation.
 * Returns 0 on success, -1 on error.
 */
int rfc2217_open(crsf_handle_t* h, const char* host, int port,
                  int baudrate, int datasize, char parity, int stopbits) {
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* ai = NULL;
    int err = getaddrinfo(host, port_str, &hints, &ai);
    if (err != 0 || !ai) {
        syslog(LOG_ERR, "RFC 2217: getaddrinfo(%s) failed: %s",
               host, gai_strerror(err));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* p = ai; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(ai);

    if (fd < 0) {
        syslog(LOG_ERR, "RFC 2217: connect to %s:%d failed: %s",
               host, port, strerror(errno));
        return -1;
    }

    h->fd = fd;
    h->type = TRANSPORT_RFC2217;
    h->u.tcp.buf_len = 0;
    h->u.tcp.sub_len = 0;
    h->u.tcp.iac_state = IAC_IDLE;
    h->u.tcp.iac_cmd = 0;

    /* Perform RFC 2217 negotiation */
    if (rfc2217_negotiate(h, baudrate, datasize, parity, stopbits) < 0) {
        syslog(LOG_ERR, "RFC 2217: negotiation failed on %s:%d", host, port);
        close(fd);
        h->fd = -1;
        return -1;
    }

    syslog(LOG_INFO, "RFC 2217: connected to %s:%d (fd=%d)", host, port, fd);
    return 0;
}

/*
 * Process incoming TCP data through the IAC state machine.
 * Returns 0 on success (more bytes may remain), -1 on error.
 *
 * Appends unescaped application bytes to h->u.tcp.buf.
 * Telnet commands and RFC 2217 notifications are handled inline.
 */
static int process_tcp_bytes(crsf_handle_t* h, const uint8_t* data, int len) {
    for (int i = 0; i < len; i++) {
        uint8_t b = data[i];

        switch (h->u.tcp.iac_state) {

        case IAC_IDLE:
            if (b == TELNET_IAC) {
                h->u.tcp.iac_state = IAC_GOT_CMD;
            } else {
                /* Regular data byte — store in buffer */
                if (h->u.tcp.buf_len < (int)sizeof(h->u.tcp.buf))
                    h->u.tcp.buf[h->u.tcp.buf_len++] = b;
            }
            break;

        case IAC_GOT_CMD:
            if (b == TELNET_IAC) {
                /* Escaped 0xFF (0xFF 0xFF in the stream) */
                if (h->u.tcp.buf_len < (int)sizeof(h->u.tcp.buf))
                    h->u.tcp.buf[h->u.tcp.buf_len++] = 0xFF;
                h->u.tcp.iac_state = IAC_IDLE;
            } else if (b == TELNET_SB) {
                h->u.tcp.iac_state = IAC_SUB;
                h->u.tcp.sub_len = 0;
            } else if (b == TELNET_WILL || b == TELNET_WONT ||
                       b == TELNET_DO   || b == TELNET_DONT) {
                h->u.tcp.iac_cmd = b;
                h->u.tcp.iac_state = IAC_GOT_OPT;
            } else {
                /* NOP, BRK, IP, AO, AYT, EC, EL, GA — ignore */
                h->u.tcp.iac_state = IAC_IDLE;
            }
            break;

        case IAC_GOT_OPT:
            /* Got option byte after WILL/WONT/DO/DONT */
            if (b == RFC2217_OPTION) {
                /* Peer acknowledged or refused COM-PORT-OPTION */
                uint8_t cmd = h->u.tcp.iac_cmd;
                if (cmd == TELNET_DO)
                    send_telnet_cmd(h, TELNET_WILL, RFC2217_OPTION);
                else if (cmd == TELNET_DONT)
                    send_telnet_cmd(h, TELNET_WONT, RFC2217_OPTION);
                /* WILL/WONT from peer — respond with DO/DONT for COM-PORT-OPTION */
                if (cmd == TELNET_WILL)
                    send_telnet_cmd(h, TELNET_DO, RFC2217_OPTION);
                else if (cmd == TELNET_WONT) {
                    /* ignore refusal */
                }
            }
            h->u.tcp.iac_state = IAC_IDLE;
            break;

        case IAC_SUB:
            if (b == TELNET_IAC) {
                h->u.tcp.iac_state = IAC_ESC;
            } else {
                if (h->u.tcp.sub_len < (int)sizeof(h->u.tcp.sub_buf))
                    h->u.tcp.sub_buf[h->u.tcp.sub_len++] = b;
            }
            break;

        case IAC_ESC:
            if (b == TELNET_SE) {
                /* End of sub-negotiation — process it */
                if (h->u.tcp.sub_len >= 2 &&
                    h->u.tcp.sub_buf[0] == RFC2217_OPTION) {
                    uint8_t subcmd = h->u.tcp.sub_buf[1];
                    uint8_t* params = h->u.tcp.sub_buf + 2;
                    int plen = h->u.tcp.sub_len - 2;

                    switch (subcmd) {
                    case RFC2217_NOTIFY_LINESTATE:
                        /* Line state notification (e.g. DSR changes) */
                        if (plen >= 1)
                            syslog(LOG_DEBUG, "RFC 2217: line state 0x%02x", params[0]);
                        break;
                    case RFC2217_NOTIFY_MODEMSTATE:
                        /* Modem state notification (e.g. CTS, DCD changes) */
                        if (plen >= 1)
                            syslog(LOG_DEBUG, "RFC 2217: modem state 0x%02x", params[0]);
                        break;
                    default:
                        syslog(LOG_DEBUG, "RFC 2217: sub-cmd %d len=%d", subcmd, plen);
                        break;
                    }
                }
                h->u.tcp.iac_state = IAC_IDLE;
            } else if (b == TELNET_IAC) {
                /* Escaped IAC inside sub-negotiation */
                if (h->u.tcp.sub_len < (int)sizeof(h->u.tcp.sub_buf))
                    h->u.tcp.sub_buf[h->u.tcp.sub_len++] = TELNET_IAC;
                h->u.tcp.iac_state = IAC_SUB;
            } else {
                /* Should not happen — IAC followed by non-SE is error */
                h->u.tcp.iac_state = IAC_IDLE;
            }
            break;
        }
    }
    return 0;
}

/*
 * Read data from an RFC 2217 connection.
 * Reads from TCP socket, processes IAC sequences, returns
 * unescaped application data.
 *
 * Returns the number of bytes read (into buf), 0 on timeout, -1 on error.
 */
int rfc2217_read(crsf_handle_t* h, void* buf, size_t len, int timeout_ms) {
    /* First, drain any remaining data from our internal buffer */
    if (h->u.tcp.buf_len > 0) {
        int to_copy = (int)len < h->u.tcp.buf_len ? (int)len : h->u.tcp.buf_len;
        memcpy(buf, h->u.tcp.buf, to_copy);
        h->u.tcp.buf_len -= to_copy;
        if (h->u.tcp.buf_len > 0)
            memmove(h->u.tcp.buf, h->u.tcp.buf + to_copy, h->u.tcp.buf_len);
        return to_copy;
    }

    /* Read from socket */
    struct pollfd pfd = { .fd = h->fd, .events = POLLIN };
    int pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0) return pr;  /* 0 = timeout, -1 = error */

    uint8_t raw[256];
    ssize_t n = recv(h->fd, raw, sizeof(raw), 0);
    if (n <= 0) return -1;

    /* Process bytes through IAC state machine */
    if (process_tcp_bytes(h, raw, (int)n) < 0)
        return -1;

    /* Return whatever application data we accumulated */
    if (h->u.tcp.buf_len == 0)
        return 0;

    int to_copy = (int)len < h->u.tcp.buf_len ? (int)len : h->u.tcp.buf_len;
    memcpy(buf, h->u.tcp.buf, to_copy);
    h->u.tcp.buf_len -= to_copy;
    if (h->u.tcp.buf_len > 0)
        memmove(h->u.tcp.buf, h->u.tcp.buf + to_copy, h->u.tcp.buf_len);
    return to_copy;
}

/*
 * Write data to an RFC 2217 connection.
 * Escapes 0xFF bytes in the data stream (send as 0xFF 0xFF).
 *
 * Returns the number of bytes written, -1 on error.
 */
int rfc2217_write(crsf_handle_t* h, const void* buf, size_t len) {
    const uint8_t* data = (const uint8_t*)buf;
    size_t total = 0;

    while (total < len) {
        /* Find next 0xFF byte that needs escaping */
        const uint8_t* next_ff = (const uint8_t*)memchr(data + total, TELNET_IAC,
                                                         len - total);
        size_t chunk = next_ff ? (size_t)(next_ff - (data + total)) : (len - total);

        /* Write unescaped chunk */
        if (chunk > 0) {
            ssize_t n = send(h->fd, data + total, chunk, 0);
            if (n < 0) return -1;
            total += n;
            if ((size_t)n < chunk) break;  /* partial write */
        }

        if (next_ff) {
            /* Write the escaped 0xFF 0xFF */
            uint8_t esc[2] = { TELNET_IAC, TELNET_IAC };
            ssize_t n = send(h->fd, esc, 2, 0);
            if (n < 0) return -1;
            total += 1;  /* consumed 1 data byte */
        }
    }
    return (int)total;
}

/*
 * Close an RFC 2217 connection.
 */
void rfc2217_close(crsf_handle_t* h) {
    if (h->fd >= 0) {
        shutdown(h->fd, SHUT_RDWR);
        close(h->fd);
    }
    h->fd = -1;
    h->type = TRANSPORT_SERIAL;
}
