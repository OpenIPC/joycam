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
 * Protocol: -p crsf (420000 baud) | -p ibus (115200 baud) | -p sbus (100000 baud)
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

/* Helper: process an EV_ABS event and update channels array. */
static void process_axis_event(int fd, const struct input_event* ev,
                               uint16_t* channels, int use_ibus) {
    struct input_absinfo abs;
    int min = 0, max = 255;
    if (ioctl(fd, EVIOCGABS(ev->code), &abs) == 0) {
        min = abs.minimum;
        max = abs.maximum;
    }
    int ch_val = use_ibus
        ? axis_to_range(ev->value, min, max,
                        IBUS_CHANNEL_MIN, IBUS_CHANNEL_MAX)
        : axis_to_crsf(ev->value, min, max);

    if (ev->code == 0) channels[0] = ch_val;
    else if (ev->code == 1) channels[1] = ch_val;
    else if (ev->code == 2) channels[2] = ch_val;
    else if (ev->code == 5) channels[3] = ch_val;
    else if (ev->code == 9) channels[4] = ch_val;
    else if (ev->code == 10) channels[5] = ch_val;
    else if (ev->code == 16) channels[6] = ch_val;
    else if (ev->code == 17) channels[7] = ch_val;
}

/* Helper: process an EV_KEY event (value != 2) and update channels array. */
static void process_button_event(const struct input_event* ev,
                                 uint16_t* channels,
                                 int crsf_min, int crsf_max) {
    int ch = button_to_channel(ev->code);
    if (ch >= 0 && ch < CRSF_NUM_CHANNELS)
        channels[ch] = ev->value ? crsf_max : crsf_min;
}

static void print_help(const char* prog) {
    printf("Joystick reader v%s\n", VERSION);
    printf("Usage:\n");
    printf("  %s -p crsf|ibus|sbus <evdev_path>                    — status line only\n", prog);
    printf("  %s -p crsf|ibus|sbus <evdev_path> -v                 — verbose (raw events)\n", prog);
    printf("  %s -p crsf|ibus|sbus <evdev_path> <serial_port>      — transmit (silent)\n", prog);
    printf("  %s -p crsf|ibus|sbus <evdev_path> <serial> -d        — transmit + status line\n", prog);
    printf("  %s -p crsf|ibus|sbus <evdev_path> <serial> -v        — transmit + raw events\n", prog);
    printf("  %s -h / --help                     — this help\n", prog);
    printf("  %s -V / --version                  — show version\n", prog);
    printf("\nFlags:\n");
    printf("  -d, --debug         show compact status line every frame\n");
    printf("  -v, --verbose       show every axis and button event\n");
    printf("  -p, --protocol <p>  REQUIRED. Output protocol: crsf, ibus, or sbus\n");
    printf("\nDefault evdev path: /dev/input/by-id/usb-...-event-joystick\n");
    printf("Find your device:  ls -l /dev/input/by-id/*-joystick\n");
}

int main(int argc, char** argv) {
    const char* device_path  = NULL;
    const char* serial_port  = NULL;
    int         debug_mode   = 0;
    int         verbose_mode = 0;
    int         protocol     = 0;  /* 0=crsf, 1=ibus, 2=sbus */
    int         protocol_set = 0;

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
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--protocol") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -p requires an argument (crsf, ibus, or sbus).\n");
                return 1;
            }
            i++;
            protocol_set = 1;
            if (strcmp(argv[i], "ibus") == 0)
                protocol = 1;
            else if (strcmp(argv[i], "sbus") == 0)
                protocol = 2;
            else if (strcmp(argv[i], "crsf") != 0) {
                fprintf(stderr, "Error: unknown protocol '%s'. Use crsf, ibus, or sbus.\n", argv[i]);
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

    if (!protocol_set) {
        fprintf(stderr, "Error: -p (--protocol) is required. Use crsf, ibus, or sbus.\n");
        fprintf(stderr, "       %s -h for full help.\n", argv[0]);
        return 1;
    }

    if (!device_path) {
        fprintf(stderr, "Error: missing evdev device path.\n");
        fprintf(stderr, "Usage: %s -p crsf|ibus|sbus <evdev_path> [serial_port] [-d|-v]\n", argv[0]);
        fprintf(stderr, "  Find your device: ls -l /dev/input/by-id/*-joystick\n");
        return 1;
    }

    /* Resolve display mode */
    if (!serial_port && !verbose_mode)
        debug_mode = 1;

    openlog("joystick", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "starting evdev=%s serial=%s proto=%d debug=%d verbose=%d",
           device_path, serial_port ? serial_port : "(none)",
           protocol, debug_mode, verbose_mode);

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
    int baudrate;
    if (protocol == 1) baudrate = IBUS_BAUDRATE;
    else if (protocol == 2) baudrate = B9600;  /* SBUS: will override via termios2 */
    else baudrate = CRSF_BAUDRATE;
    crsf_handle_t h = {-1};
    if (serial_port && crsf_serial_open(serial_port, &h, O_WRONLY, baudrate) < 0) {
        close(fd);
        closelog();
        return 1;
    }
    /* Override baudrate for SBUS (custom 100000 via termios2). */
    if (serial_port && protocol == 2) {
        if (set_baudrate_custom(h.fd, SBUS_BAUDRATE) < 0) {
            fprintf(stderr, "Error: cannot set 100000 baud on %s\n", serial_port);
            close(fd);
            closelog();
            return 1;
        }
    }

    const char* proto_name = protocol == 1 ? "IBUS" : protocol == 2 ? "SBUS" : "CRSF";
    if (debug_mode || verbose_mode) {
        printf("Listening on %s...\n", device_path);
        printf("Device: %s %s\n",
               dev_name[0] ? dev_name : "(unknown)",
               dev_phys[0] ? dev_phys : "");
        if (h.fd >= 0) {
            int actual_baud = protocol == 1 ? IBUS_BAUDRATE
                             : protocol == 2 ? SBUS_BAUDRATE
                             : CRSF_BAUDRATE;
            printf("  %s output on %s (%d baud)\n",
                   proto_name, serial_port, actual_baud);
        } else {
            printf("  (no serial port)\n");
        }
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
    int num_channels, crsf_mid, crsf_min, crsf_max, packet_size;
    if (protocol == 1) {
        num_channels = IBUS_NUM_CHANNELS;
        crsf_mid = IBUS_CHANNEL_MID;
        crsf_min = IBUS_CHANNEL_MIN;
        crsf_max = IBUS_CHANNEL_MAX;
        packet_size = IBUS_PACKET_SIZE;
    } else {
        num_channels = CRSF_NUM_CHANNELS;
        crsf_mid = CRSF_CHANNEL_MID;
        crsf_min = CRSF_CHANNEL_MIN;
        crsf_max = CRSF_CHANNEL_MAX;
        packet_size = protocol == 2 ? SBUS_PACKET_SIZE : CRSF_TOTAL_FRAME_SIZE;
    }
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
        /* On SYN_DROPPED, drain all pending events until SYN_REPORT
           but process axis/button events so no data is lost, then
           fall through to process the SYN_REPORT normally. */
        if (n == sizeof(ev) && ev.type == EV_SYN && ev.code == SYN_DROPPED) {
            while (read(fd, &ev, sizeof(ev)) > 0) {
                if (ev.type == EV_SYN && ev.code == SYN_REPORT)
                    break;
                if (ev.type == EV_ABS)
                    process_axis_event(fd, &ev, channels, protocol == 1);
                else if (ev.type == EV_KEY && ev.value != 2)
                    process_button_event(&ev, channels, crsf_min, crsf_max);
            }
            /* Fall through to process the SYN_REPORT. NOT continue. */
        } else if (n != sizeof(ev)) {
            continue;
        }

        /* Suppress EV_MSC (kernel-internal, not user input). */
        if (ev.type == EV_MSC) continue;

        if (ev.type == EV_ABS) {
            process_axis_event(fd, &ev, channels, protocol == 1);

            if (verbose_mode) {
                printf("Axis %d: val %d -> %s\n",
                       ev.code, ev.value, proto_name);
                fflush(stdout);
            }
        }
        else if (ev.type == EV_KEY && ev.value != 2) {
            int ch = button_to_channel(ev.code);
            process_button_event(&ev, channels, crsf_min, crsf_max);

            if (verbose_mode) {
                printf("Button %d %s -> ch%d (%s)\n",
                       ev.code, ev.value ? "pressed" : "released",
                       ch, proto_name);
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
            if (protocol == 1)
                ibus_generate_packet(packet, channels);
            else if (protocol == 2)
                sbus_generate_packet(packet, channels);
            else
                crsf_generate_rc_packet(packet, channels);
            if (h.fd >= 0) {
                int wret = crsf_write(&h, packet, packet_size, 10);
                if (wret < 0)
                    syslog(LOG_ERR, "write error on serial port");
            } else if (verbose_mode) {
                crsf_hex_dump(packet, packet_size, proto_name);
            }
        }
    }

    syslog(LOG_INFO, "shutting down");
    crsf_serial_close(&h);
    close(fd);
    closelog();
    return 0;
}
