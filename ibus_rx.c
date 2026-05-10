/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
 *
 * ibus_rx.c — IBUS (FlySky) receiver: reads UART, parses IBUS frames, displays channels
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
            printf("IBUS receiver v%s\n", VERSION);
            printf("Usage: %s <serial_port> [-d]\n", argv[0]);
            printf("  Reads IBUS (FlySky) frames from a serial port\n");
            printf("  and decodes 14 RC channels.\n");
            printf("\nOptions:\n");
            printf("  -d, --debug   hex dump every received frame\n");
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("ibus_rx v%s\n", VERSION);
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

    openlog("ibus_rx", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "starting on %s", serial_port);

    {   struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = handle_signal;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }
    signal(SIGPIPE, SIG_IGN);

    crsf_handle_t h = { .fd = -1 };
    if (crsf_serial_open(serial_port, &h, O_RDONLY, IBUS_BAUDRATE) < 0) {
        closelog();
        return 1;
    }

    ibus_channels_t channels = {0};
    uint8_t byte;
    uint8_t raw_buf[IBUS_PACKET_SIZE];
    int raw_len = 0;

    printf("Listening for IBUS data on %s...\n", serial_port);
    syslog(LOG_INFO, "listening on %s", serial_port);

    while (!stop_flag) {
        int ret = crsf_read(&h, &byte, 1, 100);
        if (ret <= 0) continue;

        if (debug_hex && raw_len < (int)sizeof(raw_buf))
            raw_buf[raw_len++] = byte;

        ret = ibus_parse_byte(byte, &channels);

        if (ret == 1) {
            /* Complete frame — hex dump if requested. */
            if (debug_hex) {
                crsf_hex_dump(raw_buf, raw_len, "IBUS");
                raw_len = 0;
            }

            printf("Channels: ");
            crsf_print_channels(channels.channels, IBUS_NUM_CHANNELS);
            printf("\n");
            syslog(LOG_INFO, "CH %d %d %d %d",
                   channels.channels[0], channels.channels[1],
                   channels.channels[2], channels.channels[3]);
        }
        else if (ret == -1) {
            /* Checksum error — reset accumulation. */
            raw_len = 0;
            printf("IBUS Checksum Error\n");
            syslog(LOG_ERR, "ibus checksum error");
        }
    }

    syslog(LOG_INFO, "shutting down");
    crsf_serial_close(&h);
    closelog();
    return 0;
}
