/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  MIT License
 *
 * crsf_tx.c — CRSF transmitter: generates test RC frames at 100 Hz
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <libserialport.h>
#include "crsf_driver.h"

static volatile sig_atomic_t stop_flag = 0;

static void handle_signal(int sig) {
    (void)sig;
    stop_flag = 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <serial_port>\n", argv[0]);
        return 1;
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
    if (sp_open(port, SP_MODE_READ_WRITE) != SP_OK) {
        fprintf(stderr, "Failed to open port: %s\n", argv[1]);
        syslog(LOG_ERR, "failed to open port %s", argv[1]);
        sp_free_port(port);
        closelog();
        return 1;
    }
    sp_set_baudrate(port, 420000);
    sp_set_parity(port, SP_PARITY_NONE);
    sp_set_bits(port, 8);
    sp_set_stopbits(port, 1);

    /* 27 = CRSF_SYNC_BYTE(1) + type(1) + payload_len(1) + payload(22) + crc(1)
       or equivalently: sizeof(uint8_t)*3 + sizeof(payload) */
    uint8_t packet[27];
#define CRSF_RC_PACKET_SIZE (3 + 22) /* sync + len + type + payload(22) + crc */
    uint16_t channels[16] = {992, 992, 992, 992}; // Midpoints

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
        sp_blocking_write(port, packet, CRSF_RC_PACKET_SIZE, 100);
        usleep(10000); // 100Hz
    }

    syslog(LOG_INFO, "shutting down");
    sp_close(port);
    sp_free_port(port);
    closelog();
    return 0;
}
