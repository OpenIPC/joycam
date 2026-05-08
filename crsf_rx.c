/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  MIT License
 *
 * crsf_rx.c — CRSF receiver: reads UART, parses CRSF frames, displays channels & link stats
 *
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include "joycrsf.h"

static volatile sig_atomic_t stop_flag = 0;

static void handle_signal(int sig) {
    (void)sig;
    stop_flag = 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <serial_port>\n", argv[0]);
        fprintf(stderr, "       %s --help\n", argv[0]);
        fprintf(stderr, "       %s --version\n", argv[0]);
        return 1;
    }

    if (argc == 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("CRSF receiver v%s\n", VERSION);
            printf("Usage: %s <serial_port>\n", argv[0]);
            printf("  Reads CRSF frames from a serial port and decodes\n");
            printf("  RC channels and link statistics.\n");
            return 0;
        }
        if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
            printf("crsf_rx v%s\n", VERSION);
            return 0;
        }
    }

    openlog("crsf_rx", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "starting on %s", argv[1]);

    {   struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = handle_signal;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }
    signal(SIGPIPE, SIG_IGN);

    struct sp_port* port;
    if (crsf_serial_open(argv[1], &port, SP_MODE_READ, 420000) < 0) {
        closelog();
        return 1;
    }

    crsf_channels_t channels = {0};
    crsf_link_stats_t stats = {0};
    uint8_t byte;

    printf("Listening for CRSF data on %s...\n", argv[1]);
    syslog(LOG_INFO, "listening on %s", argv[1]);

    while (!stop_flag) {
        int ret = sp_blocking_read(port, &byte, 1, 100);
        if (ret <= 0) continue;

        ret = crsf_parse_byte(byte, &channels, &stats);

        if (ret == 1) {
            printf("\nChannels: ");
            crsf_print_channels(channels.channels, CRSF_NUM_CHANNELS);
            printf("\n");
            syslog(LOG_INFO, "CH %d %d %d %d",
                   channels.channels[0], channels.channels[1],
                   channels.channels[2], channels.channels[3]);
        }
        else if (ret == 2) {
            printf("Link Quality: %d%%  RSSI1: %d RSSI2: %d Power: %d\n",
                   stats.quality, stats.rssi_1, stats.rssi_2, stats.rf_power);
            syslog(LOG_INFO, "LQ %d%% RSSI1 %d RSSI2 %d PWR %d",
                   stats.quality, stats.rssi_1, stats.rssi_2, stats.rf_power);
        }
        else if (ret == -1) {
            printf("CRC Error\n");
            syslog(LOG_ERR, "CRC error");
        }
    }

    syslog(LOG_INFO, "shutting down");
    crsf_serial_close(port);
    closelog();
    return 0;
}
