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
#include <libserialport.h>
#include "JoyCRSF.h"

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

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    struct sp_port* port;
    if (sp_get_port_by_name(argv[1], &port) != SP_OK) {
        fprintf(stderr, "Failed to get port: %s\n", argv[1]);
        syslog(LOG_ERR, "failed to get port %s", argv[1]);
        closelog();
        return 1;
    }
    if (sp_open(port, SP_MODE_READ) != SP_OK) {
        fprintf(stderr, "Failed to open port: %s\n", argv[1]);
        syslog(LOG_ERR, "failed to open port %s", argv[1]);
        sp_free_port(port);
        closelog();
        return 1;
    }
    if (sp_set_baudrate(port, 420000) != SP_OK) {
        syslog(LOG_ERR, "failed to set baudrate");
        fprintf(stderr, "Error: cannot set baudrate 420000\n");
    }
    sp_set_parity(port, SP_PARITY_NONE);
    sp_set_bits(port, 8);
    sp_set_stopbits(port, 1);

    crsf_channels_t channels;
    crsf_link_stats_t stats;
    uint8_t byte;

    printf("Listening for CRSF data on %s...\n", argv[1]);
    syslog(LOG_INFO, "listening on %s", argv[1]);

    while (!stop_flag) {
        int ret = sp_blocking_read(port, &byte, 1, 100);
        if (ret <= 0) continue;

        ret = crsf_parse_byte(byte, &channels, &stats);

        if (ret == 1) {
            printf("\nChannels: ");
            for (int i = 0; i < 4; i++) {
                printf("%d:%d ", i, channels.channels[i]);
            }
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
    sp_close(port);
    sp_free_port(port);
    closelog();
    return 0;
}
