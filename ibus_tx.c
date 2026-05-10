/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
 *
 * ibus_tx.c — IBUS (FlySky) transmitter: generates test RC frames at 50 Hz
 *
 */

#include <stdio.h>
#include <stdlib.h>
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
    int sweep_ch = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("IBUS transmitter v%s\n", VERSION);
            printf("Usage: %s <serial_port> [-a <ch>]\n", argv[0]);
            printf("  Generates IBUS (FlySky) RC channel packets at 50 Hz\n");
            printf("  with a sawtooth sweep on a selected channel.\n");
            printf("\nOptions:\n");
            printf("  -a <ch>   channel to sweep (default: 0)\n");
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("ibus_tx v%s\n", VERSION);
            return 0;
        }
        if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            sweep_ch = atoi(argv[++i]);
            continue;
        }
        if (argv[i][0] != '-' && !serial_port) {
            serial_port = argv[i];
        }
    }

    if (!serial_port) {
        fprintf(stderr, "Usage: %s <serial_port> [-a <ch>]\n", argv[0]);
        fprintf(stderr, "       %s --help\n", argv[0]);
        return 1;
    }

    if (sweep_ch < 0 || sweep_ch >= IBUS_NUM_CHANNELS) {
        fprintf(stderr, "Error: channel %d out of range (0..%d)\n",
                sweep_ch, IBUS_NUM_CHANNELS - 1);
        return 1;
    }

    openlog("ibus_tx", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "starting on %s ch=%d", serial_port, sweep_ch);

    {   struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = handle_signal;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }
    signal(SIGPIPE, SIG_IGN);

    crsf_handle_t h = { .fd = -1 };
    if (crsf_serial_open(serial_port, &h, O_WRONLY, IBUS_BAUDRATE) < 0) {
        closelog();
        return 1;
    }

    uint8_t packet[IBUS_PACKET_SIZE];
    uint16_t channels[IBUS_NUM_CHANNELS];
    for (int i = 0; i < IBUS_NUM_CHANNELS; i++)
        channels[i] = IBUS_CHANNEL_MID;

    printf("Sending IBUS frames to %s... sweep on ch%d\n", serial_port, sweep_ch);
    syslog(LOG_INFO, "sending on %s ch=%d", serial_port, sweep_ch);

    uint16_t val = IBUS_CHANNEL_MIN;
    int direction = 1;

    while (!stop_flag) {
        channels[sweep_ch] = val;
        val += direction * 10;
        if (val >= IBUS_CHANNEL_MAX) { val = IBUS_CHANNEL_MAX; direction = -direction; }
        if (val <= IBUS_CHANNEL_MIN) { val = IBUS_CHANNEL_MIN; direction = -direction; }

        ibus_generate_packet(packet, channels);
        if (crsf_write(&h, packet, IBUS_PACKET_SIZE, 100) < 0) {
            syslog(LOG_ERR, "write error on port");
            break;
        }
        usleep(20000); /* 50 Hz */
    }

    syslog(LOG_INFO, "shutting down");
    crsf_serial_close(&h);
    closelog();
    return 0;
}
