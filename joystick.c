/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
 *
 * joystick.c — USB joystick reader via evdev with CRSF/IBUS output
 *
 * Modes (debug_mode = status line only, verbose = raw events):
 *   debug            ./joystick <evdev>              — status line only
 *   verbose          ./joystick <evdev> -v           — raw event spam
 *   transmit         ./joystick <evdev> <serial>     — silent tx
 *   tx+status        ./joystick <evdev> <serial> -d  — tx + status line
 *   tx+verbose       ./joystick <evdev> <serial> -v  — tx + raw events
 *
 * Protocol: -p crsf (default, 420000 baud) | -p ibus (115200 baud)
 *
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <linux/input.h>
#include "joycam.h"

static volatile sig_atomic_t stop_flag = 0;

static void handle_signal(int sig) {
    (void)sig;
    stop_flag = 1;
}

/* axis_to_crsf and button_to_channel are now in joycam.c */

static void print_help(const char* prog) {
    printf("Joystick reader v%s\n", VERSION);
    printf("Usage:\n");
    printf("  %s <evdev_path>                    — status line only\n", prog);
    printf("  %s <evdev_path> -v                 — verbose (raw events)\n", prog);
    printf("  %s <evdev_path> <serial_port> [-p crsf|ibus]  — transmit (silent)\n", prog);
    printf("  %s <evdev_path> <serial> -d [-p crsf|ibus]    — transmit + status line\n", prog);
    printf("  %s <evdev_path> <serial> -v [-p crsf|ibus]    — transmit + raw events\n", prog);
    printf("  %s -h / --help                     — this help\n", prog);
    printf("  %s -V / --version                  — show version\n", prog);
    printf("\nFlags:\n");
    printf("  -d, --debug         show compact status line every frame\n");
    printf("  -v, --verbose       show every axis and button event\n");
    printf("  -p, --protocol <p>  output protocol: crsf (default) or ibus\n");
    printf("\nDefault evdev path: /dev/input/by-id/usb-...-event-joystick\n");
    printf("Find your device:  ls -l /dev/input/by-id/*-joystick\n");
}

int main(int argc, char** argv) {
    const char* device_path  = NULL;
    const char* serial_port  = NULL;
    int         debug_mode   = 0;
    int         verbose_mode = 0;
    int         use_ibus     = 0;

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
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose_mode = 1;
            continue;
        }
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--protocol") == 0)
            && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "ibus") == 0)
                use_ibus = 1;
            else if (strcmp(argv[i], "crsf") != 0) {
                fprintf(stderr, "Error: unknown protocol '%s'. Use crsf or ibus.\n", argv[i]);
                return 1;
            }
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
        printf("Usage: %s <evdev_path> [serial_port] [-d|-v]\n", argv[0]);
        printf("  Default evdev: %s\n", device_path);
        printf("  Find your device: ls -l /dev/input/by-id/*-joystick\n");
    }

    /* Resolve display mode */
    if (!serial_port && !verbose_mode)
        debug_mode = 1;  /* status line by default in console mode */

    openlog("joystick", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "starting evdev=%s serial=%s debug=%d verbose=%d",
           device_path, serial_port ? serial_port : "(none)",
           debug_mode, verbose_mode);

    /* sigaction without SA_RESTART so Ctrl+C interrupts blocking reads. */
    {   struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = handle_signal;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }
    signal(SIGPIPE, SIG_IGN);

    /* --- Open evdev device --- */
    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        syslog(LOG_ERR, "failed to open evdev %s", device_path);
        closelog();
        return 1;
    }

    if (ioctl(fd, EVIOCGVERSION, &(unsigned int){0}) < 0) {
        fprintf(stderr, "Error: %s is not an evdev device (%s)\n",
                device_path, strerror(errno));
        close(fd);
        closelog();
        return 1;
    }

    /* Query device name and phys from evdev via ioctl. */
    char dev_name[256] = {0};
    char dev_phys[256] = {0};
    ioctl(fd, EVIOCGNAME(sizeof(dev_name)), dev_name);
    ioctl(fd, EVIOCGPHYS(sizeof(dev_phys)), dev_phys);

    /* --- Open serial port (optional) --- */
    int baudrate = use_ibus ? IBUS_BAUDRATE : CRSF_BAUDRATE;
    crsf_handle_t h = {-1};
    if (serial_port && crsf_serial_open(serial_port, &h, O_WRONLY, baudrate) < 0) {
        close(fd);
        closelog();
        return 1;
    }

    if (debug_mode || verbose_mode) {
        printf("Listening on %s...\n", device_path);
        printf("Device: %s %s\n",
               dev_name[0] ? dev_name : "(unknown)",
               dev_phys[0] ? dev_phys : "");
        if (h.fd >= 0)
            printf("  %s output on %s (%d baud)\n",
                   use_ibus ? "IBUS" : "CRSF", serial_port, baudrate);
        else
            printf("  (no serial port)\n");
        fflush(stdout);
    }
    syslog(LOG_INFO, "started evdev=%s", device_path);

    /* Grab the device so events don't get stolen. */
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        fprintf(stderr, "Warning: EVIOCGRAB failed: %s\n", strerror(errno));
        syslog(LOG_WARNING, "EVIOCGRAB failed on %s: %s", device_path, strerror(errno));
    }

    /* --- Main loop --- */
    struct input_event ev;
    int num_channels = use_ibus ? IBUS_NUM_CHANNELS : CRSF_NUM_CHANNELS;
    int crsf_mid     = use_ibus ? IBUS_CHANNEL_MID : CRSF_CHANNEL_MID;
    int crsf_min     = use_ibus ? IBUS_CHANNEL_MIN : CRSF_CHANNEL_MIN;
    int crsf_max     = use_ibus ? IBUS_CHANNEL_MAX : CRSF_CHANNEL_MAX;
    int packet_size  = use_ibus ? IBUS_PACKET_SIZE : CRSF_TOTAL_FRAME_SIZE;
    uint16_t channels[CRSF_NUM_CHANNELS];
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++)
        channels[i] = crsf_mid;
    uint8_t packet[32];  /* max(CRSF_TOTAL_FRAME_SIZE, IBUS_PACKET_SIZE) */

    while (!stop_flag) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr = poll(&pfd, 1, 10);  /* 10 ms timeout for signal checking */
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue;  /* timeout — no data, re-check stop_flag */

        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        /* On SYN_DROPPED, drain all pending events until SYN_REPORT. */
        if (n == sizeof(ev) && ev.type == EV_SYN && ev.code == SYN_DROPPED) {
            while (read(fd, &ev, sizeof(ev)) > 0) {
                if (ev.type == EV_SYN && ev.code == SYN_REPORT)
                    break;
            }
            continue;
        }
        if (n != sizeof(ev)) continue;

        /* Suppress EV_MSC (kernel-internal, not user input). */
        if (ev.type == EV_MSC) continue;

        if (ev.type == EV_ABS) {
            struct input_absinfo abs;
            int min = 0, max = 255;
            if (ioctl(fd, EVIOCGABS(ev.code), &abs) == 0) {
                min = abs.minimum;
                max = abs.maximum;
            }
            int ch_val = use_ibus
                ? axis_to_range(ev.value, min, max,
                                IBUS_CHANNEL_MIN, IBUS_CHANNEL_MAX)
                : axis_to_crsf(ev.value, min, max);

            if (ev.code == 0) channels[0] = ch_val;
            else if (ev.code == 1) channels[1] = ch_val;
            else if (ev.code == 2) channels[2] = ch_val;
            else if (ev.code == 5) channels[3] = ch_val;
            else if (ev.code == 9) channels[4] = ch_val;
            else if (ev.code == 10) channels[5] = ch_val;
            else if (ev.code == 16) channels[6] = ch_val;
            else if (ev.code == 17) channels[7] = ch_val;

            if (verbose_mode) {
                printf("Axis %d: val %d [%d..%d] -> %s %d\n",
                       ev.code, ev.value, min, max,
                       use_ibus ? "IBUS" : "CRSF", ch_val);
                fflush(stdout);
            }
        }
        else if (ev.type == EV_KEY && ev.value != 2) {
            int ch = button_to_channel(ev.code);
            if (ch >= 0 && ch < CRSF_NUM_CHANNELS)
                channels[ch] = ev.value ? crsf_max : crsf_min;

            if (verbose_mode) {
                printf("Button %d %s -> ch%d (%s)\n",
                       ev.code, ev.value ? "pressed" : "released",
                       ch, use_ibus ? "IBUS" : "CRSF");
                fflush(stdout);
            }
        }
        else if (ev.type == EV_SYN) {
            /* Status line — all channels. */
            if (debug_mode) {
                printf("CH ");
                crsf_print_channels(channels, num_channels);
                printf("\n");
                fflush(stdout);
            }

            /* Generate and send protocol frame. */
            if (use_ibus)
                ibus_generate_packet(packet, channels);
            else
                crsf_generate_rc_packet(packet, channels);
            if (h.fd >= 0) {
                int wret = crsf_write(&h, packet, packet_size, 10);
                if (wret < 0)
                    syslog(LOG_ERR, "write error on serial port");
            } else if (verbose_mode) {
                crsf_hex_dump(packet, packet_size,
                              use_ibus ? "IBUS" : "CRSF");
            }
        }
    }

    syslog(LOG_INFO, "shutting down");
    crsf_serial_close(&h);
    close(fd);
    closelog();
    return 0;
}
