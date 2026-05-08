/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  MIT License
 *
 * crsf_tx.c — CRSF transmitter: generates test RC frames at 100 Hz
 *
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
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
            printf("CRSF transmitter v%s\n", VERSION);
            printf("Usage: %s <serial_port>\n", argv[0]);
            printf("  Generates CRSF RC channel packets at 100 Hz\n");
            printf("  with a sawtooth sweep on channel 0.\n");
            return 0;
        }
        if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
            printf("crsf_tx v%s\n", VERSION);
            return 0;
        }
    }

    openlog("crsf_tx", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "starting on %s", argv[1]);

    {   struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = handle_signal;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }
    signal(SIGPIPE, SIG_IGN);

    crsf_handle_t h = {-1};
    if (crsf_serial_open(argv[1], &h, O_WRONLY, 420000) < 0) {
        closelog();
        return 1;
    }

    uint8_t packet[CRSF_TOTAL_FRAME_SIZE];
    /* All channels initialised to mid-point (992). Never leave at 0 — some
       flight controllers treat 0 as failsafe. */
    uint16_t channels[CRSF_NUM_CHANNELS] = {992, 992, 992, 992, 992, 992, 992, 992,
                                            992, 992, 992, 992, 992, 992, 992, 992};

    printf("Sending CRSF frames to %s...\n", argv[1]);
    syslog(LOG_INFO, "sending on %s", argv[1]);

    uint16_t val = 172;
    int direction = 1;

    while (!stop_flag) {
        // Simple servo test: sweep from min to max
        channels[0] = val;
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
