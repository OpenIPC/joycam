/*
 *
 * Copyright (c) OpenIPC  https://openipc.org  MIT License
 *
 * joystick.c — USB joystick reader via evdev, maps axes to CRSF range
 *
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <libevdev/libevdev.h>
#include "crsf_driver.h"

static volatile sig_atomic_t stop_flag = 0;

static void handle_signal(int sig) {
    (void)sig;
    stop_flag = 1;
}

int main(int argc, char** argv) {
    const char* device_path = "/dev/input/by-id/usb-...-event-joystick";

    if (argc >= 2) {
        device_path = argv[1];
    } else {
        printf("Usage: %s [device_path]\n", argv[0]);
        printf("  Default: %s\n", device_path);
        printf("  Find your device: ls -l /dev/input/by-id/*-joystick\n");
    }

    openlog("joystick", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "starting on %s", device_path);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        syslog(LOG_ERR, "failed to open %s", device_path);
        closelog();
        return 1;
    }

    struct libevdev* dev;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        fprintf(stderr, "Failed to init libevdev: %s\n", strerror(-rc));
        syslog(LOG_ERR, "libevdev init failed: %s", strerror(-rc));
        close(fd);
        closelog();
        return 1;
    }

    printf("Listening for joystick events on %s...\n", device_path);
    syslog(LOG_INFO, "listening on %s", device_path);

    struct input_event ev;
    while (!stop_flag) {
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (ev.type == EV_ABS) {
                // Map axis values to CRSF channel range (172-1811)
                // For Linux evdev, values are typically -32767 to 32767
                int channel_val = 172 + (ev.value + 32767) * (1811 - 172) / 65534;
                printf("Axis %d: value %d -> CRSF %d\n", ev.code, ev.value, channel_val);
                syslog(LOG_INFO, "Axis %d value %d crsf %d", ev.code, ev.value, channel_val);
            }
            else if (ev.type == EV_KEY && ev.value != 2) {
                printf("Button %d %s\n", ev.code, ev.value ? "pressed" : "released");
                syslog(LOG_INFO, "Button %d %s", ev.code, ev.value ? "pressed" : "released");
            }
        }
        else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // Device was disconnected/reconnected — drain sync events
            while (libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &ev) == LIBEVDEV_READ_STATUS_SUCCESS)
                ;
            syslog(LOG_WARNING, "joystick sync event handled");
        }
        usleep(1000);
    }

    syslog(LOG_INFO, "shutting down");
    libevdev_free(dev);
    close(fd);
    closelog();
    return 0;
}
