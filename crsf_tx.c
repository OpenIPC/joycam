/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
 *
 * crsf_tx.c — CRSF transmitter: generates test RC frames at 100 Hz
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
            printf("CRSF transmitter v%s\n\n", VERSION);
            printf("Usage:  %s <port> [-a <ch>]\n\n", argv[0]);
            printf("Generates CRSF RC channel packets at 100 Hz with a\n");
            printf("sawtooth sweep on a selected channel.\n\n");
            printf("Arguments:\n");
            printf("  <port>   serial device   e.g. /dev/ttyS0\n");
            printf("           tcp:host:port   e.g. tcp:192.168.1.5:2217\n\n");
            printf("Options:\n");
            printf("  -a <ch>   channel to sweep (default: 0)\n");
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("crsf_tx v%s\n", VERSION);
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

    if (sweep_ch < 0 || sweep_ch >= CRSF_NUM_CHANNELS) {
        fprintf(stderr, "Error: channel %d out of range (0..%d)\n",
                sweep_ch, CRSF_NUM_CHANNELS - 1);
        return 1;
    }

    openlog("crsf_tx", LOG_PID | LOG_CONS, LOG_DAEMON);
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
    if (crsf_serial_open(serial_port, &h, O_WRONLY, 420000) < 0) {
        closelog();
        return 1;
    }

    uint8_t packet[CRSF_TOTAL_FRAME_SIZE];
    /* All channels initialised to mid-point (992). Never leave at 0 — some
       flight controllers treat 0 as failsafe. */
    uint16_t channels[CRSF_NUM_CHANNELS] = {992, 992, 992, 992, 992, 992, 992, 992,
                                            992, 992, 992, 992, 992, 992, 992, 992};

    printf("Sending CRSF frames to %s... sweep on ch%d\n", serial_port, sweep_ch);
    syslog(LOG_INFO, "sending on %s ch=%d", serial_port, sweep_ch);

    uint16_t val = 172;
    int direction = 1;

    while (!stop_flag) {
        // Simple servo test: sweep from min to max
        channels[sweep_ch] = val;
        val += direction * 10;
        if (val >= 1811) { val = 1811; direction = -direction; }
        if (val <= 172) { val = 172; direction = -direction; }

        crsf_generate_rc_packet(packet, channels);
        if (crsf_write(&h, packet, CRSF_TOTAL_FRAME_SIZE, 100) < 0) {
            syslog(LOG_ERR, "write error on port");
            break;
        }
        usleep(10000); // 100Hz
    }

    syslog(LOG_INFO, "shutting down");
    crsf_serial_close(&h);
    closelog();
    return 0;
}
