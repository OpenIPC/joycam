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
#include <syslog.h>
#include <libserialport.h>
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
    if (sp_open(port, SP_MODE_WRITE) != SP_OK) {
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

    uint8_t packet[CRSF_TOTAL_FRAME_SIZE];
    /* All channels initialised to mid-point (992). Never leave at 0 — some
       flight controllers treat 0 as failsafe. */
    uint16_t channels[16] = {992, 992, 992, 992, 992, 992, 992, 992,
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
        if (sp_blocking_write(port, packet, CRSF_TOTAL_FRAME_SIZE, 100) < 0) {
            syslog(LOG_ERR, "write error on port");
            break;
        }
        usleep(10000); // 100Hz
    }

    syslog(LOG_INFO, "shutting down");
    sp_close(port);
    sp_free_port(port);
    closelog();
    return 0;
}
