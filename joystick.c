/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  MIT License
 *
 * joystick.c — USB joystick reader via evdev with CRSF output
 *
 * Modes:
 *   debug         ./joystick <evdev_path>            — print events only
 *   transmit      ./joystick <evdev_path> <serial>   — send CRSF over UART
 *   tx+debug      ./joystick <evdev_path> <serial> -d — both
 *
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <libserialport.h>
#include <libevdev/libevdev.h>
#include "joycrsf.h"

static volatile sig_atomic_t stop_flag = 0;

static void handle_signal(int sig) {
    (void)sig;
    stop_flag = 1;
}

/* Map evdev axis value (-32767..32767) to CRSF range (172..1811). */
static int axis_to_crsf(int value) {
    return 172 + (value + 32767) * (1811 - 172) / 65534;
}

static void print_help(const char* prog) {
    printf("Joystick reader v%s\n", VERSION);
    printf("Usage:\n");
    printf("  %s <evdev_path>                — debug, print events to console\n", prog);
    printf("  %s <evdev_path> <serial_port>  — full chain, send CRSF via UART\n", prog);
    printf("  %s <evdev_path> <serial> -d    — transmit + debug console\n", prog);
    printf("  %s -h / --help                 — this help\n", prog);
    printf("  %s -V / --version              — show version\n", prog);
    printf("\nDefault evdev path: /dev/input/by-id/usb-...-event-joystick\n");
    printf("Find your device:  ls -l /dev/input/by-id/*-joystick\n");
}

int main(int argc, char** argv) {
    const char* device_path  = NULL;
    const char* serial_port  = NULL;
    int         debug_mode   = 0;

    /* --- Parse arguments --- */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("joystick v%s\n", VERSION);
            return 0;
        }
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
            continue;
        }
        if (argv[i][0] != '-') {
            if (!device_path)
                device_path = argv[i];
            else if (!serial_port)
                serial_port = argv[i];
        }
    }

    if (!device_path) {
        device_path = "/dev/input/by-id/usb-...-event-joystick";
        printf("Usage: %s <evdev_path> [serial_port] [-d]\n", argv[0]);
        printf("  Default evdev: %s\n", device_path);
        printf("  Find your device: ls -l /dev/input/by-id/*-joystick\n");
    }

    /* Resolve debug mode:
       - no serial port       → debug ON  (console-only mode)
       - serial port, no  -d  → debug OFF (silent transmit)
       - serial port, with -d → debug ON  (transmit + console) */
    if (!serial_port)
        debug_mode = 1;

    openlog("joystick", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "starting evdev=%s serial=%s debug=%d",
           device_path, serial_port ? serial_port : "(none)", debug_mode);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    /* --- Open evdev device --- */
    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        syslog(LOG_ERR, "failed to open evdev %s", device_path);
        closelog();
        return 1;
    }

    /* Verify device is a valid evdev before handing it to libevdev. */
    unsigned int evdev_version;
    if (ioctl(fd, EVIOCGVERSION, &evdev_version) < 0) {
        fprintf(stderr, "Error: %s is not an evdev device (%s)\n",
                device_path, strerror(errno));
        fprintf(stderr, "  Check the device: ls -l %s\n", device_path);
        fprintf(stderr, "  Available devices: ls -l /dev/input/by-id/\n");
        close(fd);
        closelog();
        return 1;
    }

    struct libevdev* dev;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        fprintf(stderr, "Error: libevdev init failed for %s: %s (rc=%d)\n",
                device_path, strerror(-rc), rc);
        syslog(LOG_ERR, "libevdev init failed for %s: %s",
               device_path, strerror(-rc));
        close(fd);
        closelog();
        return 1;
    }
    libevdev_grab(dev, LIBEVDEV_GRAB);

    /* --- Open serial port (optional) --- */
    struct sp_port* port = NULL;
    if (serial_port) {
        if (sp_get_port_by_name(serial_port, &port) != SP_OK) {
            fprintf(stderr, "Failed to get serial port: %s\n", serial_port);
            syslog(LOG_ERR, "failed to get serial port %s", serial_port);
            libevdev_free(dev);
            close(fd);
            closelog();
            return 1;
        }
        if (sp_open(port, SP_MODE_WRITE) != SP_OK) {
            fprintf(stderr, "Failed to open serial port: %s\n", serial_port);
            syslog(LOG_ERR, "failed to open serial port %s", serial_port);
            sp_free_port(port);
            libevdev_free(dev);
            close(fd);
            closelog();
            return 1;
        }
        if (sp_set_baudrate(port, 420000) != SP_OK) {
            syslog(LOG_ERR, "failed to set baudrate on %s", serial_port);
            fprintf(stderr, "Error: cannot set baudrate 420000 on %s\n", serial_port);
        }
        sp_set_parity(port, SP_PARITY_NONE);
        sp_set_bits(port, 8);
        sp_set_stopbits(port, 1);
    }

    if (debug_mode) {
        printf("Listening for joystick events on %s...\n", device_path);
        if (port)
            printf("  CRSF output on %s\n", serial_port);
    }
    syslog(LOG_INFO, "started evdev=%s", device_path);

    /* --- Main loop --- */
    struct input_event ev;
    uint16_t channels[CRSF_NUM_CHANNELS] = {992, 992, 992, 992, 992, 992, 992, 992,
                                            992, 992, 992, 992, 992, 992, 992, 992};
    uint8_t packet[CRSF_TOTAL_FRAME_SIZE];
    unsigned int loop_count = 0;

    while (!stop_flag) {
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == -EAGAIN) {
            /* No events available in non-blocking mode — this is normal. */
        }
        else if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (ev.type == EV_ABS) {
                int crsf_val = axis_to_crsf(ev.value);

                /* Map first 8 axes to CRSF channels 0-7. */
                if (ev.code < 8)
                    channels[ev.code] = crsf_val;

                if (debug_mode)
                    printf("Axis %d: value %d -> CRSF %d\n",
                           ev.code, ev.value, crsf_val);
            }
            else if (ev.type == EV_KEY && ev.value != 2) {
                if (debug_mode)
                    printf("Button %d %s\n",
                           ev.code, ev.value ? "pressed" : "released");

                /* Map first two buttons (BTN_SOUTH=304, BTN_EAST=305 are
                   common on many controllers) to ch8/ch9.  Also accept
                   legacy codes 0/1 for older devices. */
                if (ev.code == 0 || ev.code == 304)
                    channels[8] = ev.value ? 1811 : 172;
                if (ev.code == 1 || ev.code == 305)
                    channels[9] = ev.value ? 1811 : 172;
                if (ev.code == 307)
                    channels[10] = ev.value ? 1811 : 172;
                if (ev.code == 308)
                    channels[11] = ev.value ? 1811 : 172;
            }
            else if (ev.type == EV_SYN) {
                /* SYN events are normal — do not spam. */
            }
            else if (debug_mode) {
                printf("Event type=%d code=%d value=%d\n",
                       ev.type, ev.code, ev.value);
            }

            /* Send CRSF frame via serial port if available. */
            if (port) {
                crsf_generate_rc_packet(packet, channels);
                int wret = sp_blocking_write(port, packet, CRSF_TOTAL_FRAME_SIZE, 10);
                if (wret < 0) {
                    syslog(LOG_ERR, "write error on serial port");
                }
            }
        }
        else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            while (libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &ev)
                   == LIBEVDEV_READ_STATUS_SUCCESS)
                ;
            syslog(LOG_WARNING, "joystick sync event handled");
        }
        else if (debug_mode) {
            printf("libevdev: rc=%d (%s)\n", rc, strerror(-rc));
        }

        /* Heartbeat — show current state every ~1000 iterations (~1 sec). */
        if (debug_mode && (++loop_count % 1000 == 0)) {
            printf("\rState | Axes: 0:%-4d 1:%-4d 2:%-4d 3:%-4d 4:%-4d 5:%-4d 6:%-4d 7:%-4d | "
                   "Btns: 8:%-4d 9:%-4d 10:%-4d 11:%-4d  \n",
                   channels[0], channels[1], channels[2], channels[3],
                   channels[4], channels[5], channels[6], channels[7],
                   channels[8], channels[9], channels[10], channels[11]);
            fflush(stdout);
        }
        usleep(1000);
    }

    syslog(LOG_INFO, "shutting down");
    if (port) {
        sp_close(port);
        sp_free_port(port);
    }
    libevdev_free(dev);
    close(fd);
    closelog();
    return 0;
}
