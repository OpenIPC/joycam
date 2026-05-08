/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
 *
 * sbus_rx.c — SBUS (Futaba) receiver: reads UART, parses SBUS frames, displays channels
 *
 * Note: SBUS uses inverted UART signal at 100000 baud, 8E2.
 * Most on-SoC UARTs handle inversion automatically; USB-UART adapters
 * may require an external hardware inverter.
 *
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <syslog.h>
#include "joycam.h"

static volatile sig_atomic_t stop_flag = 0;

static void handle_signal(int sig) {
    (void)sig;
    stop_flag = 1;
}

int main(int argc, char** argv) {
    const char* serial_port = NULL;
    int debug_hex = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("SBUS receiver v%s\n", VERSION);
            printf("Usage: %s <serial_port> [-d]\n", argv[0]);
            printf("  Reads SBUS (Futaba) frames from a serial port\n");
            printf("  and decodes 16 RC channels.\n");
            printf("\nOptions:\n");
            printf("  -d, --debug   hex dump every received frame\n");
            printf("\nNote: SBUS uses 100000 baud 8E2 with inverted signal.\n");
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("sbus_rx v%s\n", VERSION);
            return 0;
        }
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_hex = 1;
            continue;
        }
        if (argv[i][0] != '-' && !serial_port) {
            serial_port = argv[i];
        }
    }

    if (!serial_port) {
        fprintf(stderr, "Usage: %s <serial_port> [-d]\n", argv[0]);
        fprintf(stderr, "       %s --help\n", argv[0]);
        return 1;
    }

    openlog("sbus_rx", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "starting on %s", serial_port);

    {   struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = handle_signal;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }
    signal(SIGPIPE, SIG_IGN);

    crsf_handle_t h = {-1};
    if (crsf_serial_open(serial_port, &h, O_RDONLY, B9600) < 0) {
        closelog();
        return 1;
    }
    /* Override with custom 100000 baud via termios2. */
    if (set_baudrate_custom(h.fd, SBUS_BAUDRATE) < 0) {
        fprintf(stderr, "Error: cannot set 100000 baud on %s\n", serial_port);
        crsf_serial_close(&h);
        closelog();
        return 1;
    }

    crsf_channels_t channels = {0};
    uint8_t byte;
    uint8_t raw_buf[SBUS_PACKET_SIZE];
    int raw_len = 0;

    printf("Listening for SBUS data on %s...\n", serial_port);
    syslog(LOG_INFO, "listening on %s", serial_port);

    while (!stop_flag) {
        int ret = crsf_read(&h, &byte, 1, 100);
        if (ret <= 0) continue;

        if (debug_hex && raw_len < (int)sizeof(raw_buf))
            raw_buf[raw_len++] = byte;

        ret = sbus_parse_byte(byte, &channels);

        if (ret == 1) {
            if (debug_hex) {
                crsf_hex_dump(raw_buf, raw_len, "SBUS");
                raw_len = 0;
            }

            printf("Channels: ");
            crsf_print_channels(channels.channels, SBUS_NUM_CHANNELS);
            printf("\n");
            syslog(LOG_INFO, "CH %d %d %d %d",
                   channels.channels[0], channels.channels[1],
                   channels.channels[2], channels.channels[3]);
        }
        else if (ret == -1) {
            raw_len = 0;
            syslog(LOG_ERR, "sbus frame error");
        }
    }

    syslog(LOG_INFO, "shutting down");
    crsf_serial_close(&h);
    closelog();
    return 0;
}
