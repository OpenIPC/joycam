# JoyCam

**JoyCam** — bridge your joystick, receiver, or transmitter to the CRSF
(Crossfire) and IBUS (FlySky) protocols on Linux.  
Receive RC channels and link telemetry from an ELRS / TBS Crossfire receiver,
generate IBUS control packets, or read a USB joystick and map its axes
to channel values — all in one toolbox.

Optimised for embedded Linux and **OpenIPC-compatible devices** (IP cameras, NVRs
based on Sigmastar, Goke, Hisilicon SoCs). Run `crsf_rx` or `ibus_rx` directly
on the camera's UART — no separate computer needed.

## Tools

| Tool | Description | OpenIPC |
|------|-------------|---------|
| `crsf_rx` | Reads CRSF frames from a serial port, parses 16 RC channels (11-bit) and link statistics (RSSI, LQ, RF power). | ✅ |
| `crsf_tx` | Generates CRSF RC channel packets at 100 Hz with a sawtooth sweep on a selected channel. | ✅ |
| `ibus_rx` | Reads IBUS (FlySky) frames from a serial port, parses 14 RC channels. | ✅ |
| `ibus_tx` | Generates IBUS RC channel packets at 50 Hz with a sawtooth sweep. | ✅ |
| `joystick` | Reads a USB joystick/gamepad via evdev and maps axis values to the CRSF (172–1811) or IBUS (1000–2000) range. Can transmit directly over UART. | ⚠️ needs USB host |

## Requirements

- Linux (tested on Ubuntu 22.04 / Debian 12, OpenIPC firmware)
- gcc with GNU99 support (or cross-compiler for target SoC)
- **No external libraries.** Pure POSIX + Linux ioctl.
  Only depends on the C standard library and kernel headers
  (`linux/input.h` for evdev support).

### Cross-compilation for OpenIPC (arm-linux-gnueabihf / aarch64-linux-gnu)

```bash
CROSS=arm-linux-gnueabihf- make
```

## Build

```bash
make
```

Five binaries are produced: `crsf_rx`, `crsf_tx`, `ibus_rx`, `ibus_tx`, `joystick`.

## Usage

### Receive CRSF data

```bash
./crsf_rx /dev/ttyUSB0 [-d]
```

Output:

```
Listening for CRSF data on /dev/ttyUSB0...
Channels: 0:992 1:1020 2:988 3:1700 4:992 5:992 6:992 7:992 8:992 9:992 10:992 11:992 12:992 13:992 14:992 15:992
Link Quality: 99%  RSSI1: -45 RSSI2: -48 Power: 100
```

Use `-d` to hex dump every raw CRSF frame alongside the decoded channels.

On OpenIPC cameras the ELRS receiver is typically connected to the camera's
UART1 or UART2 (e.g. `/dev/ttyAMA1`).

### Receive IBUS data

```bash
./ibus_rx /dev/ttyUSB0 [-d]
```

Output:

```
Listening for IBUS data on /dev/ttyUSB0...
Channels: 0:1500 1:1500 2:1500 3:1500 4:1500 5:1500 6:1500 7:1500  | 8:1500 9:1500 10:1500 11:1500 12:1500 13:1500
```

IBUS uses 115200 baud, 14 channels in the 1000–2000 range.

### Transmit test frames

CRSF (100 Hz):

```bash
./crsf_tx /dev/ttyACM0 [-a <ch>]
```

IBUS (50 Hz):

```bash
./ibus_tx /dev/ttyACM0 [-a <ch>]
```

Channel 0 sweeps by default. Use `-a <ch>` to sweep a different channel.

### Read joystick / full-chain bridge

`joystick` reads a USB joystick via evdev and maps its axes/buttons to
channel values. It supports both **CRSF** (default) and **IBUS** (`-p ibus`).
Five display modes are available:

| Mode | Command | Behaviour |
|------|---------|-----------|
| Status line | `./joystick <evdev>` | Show all channels every frame |
| Verbose | `./joystick <evdev> -v` | Print every axis/button event + HEX dump |
| Transmit | `./joystick <evdev> <serial> [-p crsf\|ibus]` | Send frames silently |
| Tx + status | `./joystick <evdev> <serial> -d [-p crsf\|ibus]` | Send + status line |
| Tx + verbose | `./joystick <evdev> <serial> -v [-p crsf\|ibus]` | Send + raw events |

Find your evdev device with:

```bash
ls -l /dev/input/by-id/*-joystick
```

Examples:

```bash
# Debug — see axis/button events (CRSF, default)
./joystick /dev/input/by-id/usb-045e_028e-event-joystick

# Silent transmit — send IBUS over UART
./joystick /dev/input/by-id/usb-045e_028e-event-joystick /dev/ttyS0 -p ibus

# Debug + transmit (CRSF)
./joystick /dev/input/by-id/usb-045e_028e-event-joystick /dev/ttyS0 -d
```

## Testing / Verification

### Virtual serial port (no hardware required)

Create a virtual serial pair with `socat`:

```bash
socat -d -d pty,raw,echo=0,link=/tmp/ttyV0 \
            pty,raw,echo=0,link=/tmp/ttyV1
```

In terminal **A** — transmit test frames:

```bash
# CRSF
./crsf_tx /tmp/ttyV0

# or IBUS
./ibus_tx /tmp/ttyV0
```

In terminal **B** — receive and decode:

```bash
# CRSF
./crsf_rx /tmp/ttyV1

# or IBUS
./ibus_rx /tmp/ttyV1
```

### Full chain: joystick → receiver (loopback)

Example loopback test using a virtual serial pair:

1. **Create a virtual serial pair:**

```bash
socat -d -d pty,raw,echo=0,link=/tmp/ttyV0 \
            pty,raw,echo=0,link=/tmp/ttyV1
```

2. **In terminal A** — run the joystick bridge:

```bash
# CRSF
./joystick /dev/input/by-id/usb-045e_028e-event-joystick /tmp/ttyV0 -d

# or IBUS
./joystick /dev/input/by-id/usb-045e_028e-event-joystick /tmp/ttyV0 -d -p ibus
```

3. **In terminal B** — receive frames:

```bash
# CRSF
./crsf_rx /tmp/ttyV1

# or IBUS
./ibus_rx /tmp/ttyV1
```

Move joystick sticks — the receiver shows live channel values.

### Axis mapping (CRSF / IBUS)

| evdev code | Meaning | CRSF channel | IBUS channel |
|-----------:|---------|:------------:|:------------:|
| 0 | Left stick X (LX) | ch0 | ch0 |
| 1 | Left stick Y (LY) | ch1 | ch1 |
| 2 | Right stick X (RX) | ch2 | ch2 |
| 5 | Right stick Y (RY) | ch3 | ch3 |
| 9 | Left trigger (LT) | ch4 | ch4 |
| 10 | D-pad X axis | ch5 | ch5 |
| 16 | D-pad Y axis | ch6 | ch6 |
| 17 | Extra axis | ch7 | ch7 |

### Button mapping (CRSF / IBUS)

| evdev code | Linux name | CRSF channel | IBUS channel |
|-----------:|------------|:------------:|:------------:|
| 304 | `BTN_SOUTH` (A / cross) | ch8 | ch8 |
| 305 | `BTN_EAST` (B / circle) | ch9 | ch9 |
| 306 | `BTN_NORTH` (X / triangle) | ch10 | ch10 |
| 307 | `BTN_WEST` (Y / square) | ch11 | ch11 |
| 308 | `BTN_TL` (left bumper) | ch12 | ch12 |
| 309 | `BTN_TR` (right bumper) | ch13 | ch13 |
| 310 | `BTN_TL2` (left trigger) | ch14 | ch14 |
| 311 | `BTN_TR2` (right trigger) | ch15 | ch15 |
| 312 | `BTN_SELECT` (back) | ch16 | ch16 |
| 313 | `BTN_START` (start) | ch17 | ch17 |
| 314 | `BTN_THUMBL` (left stick click) | ch18 | ch18 |
| 315 | `BTN_THUMBR` (right stick click) | ch19 | ch19 |

All other buttons are ignored.

## Project structure

```
.
├── Makefile              — Build system
├── joycam.h              — Master header: CRSF + IBUS constants, structures, prototypes
├── joycam.c              — Shared utils: serial I/O, channel display, axis/button mapping
├── joycrsf.c             — CRSF protocol: CRC8, packet parser FSM, packet generator
├── joyibus.c             — IBUS protocol: checksum, packet parser FSM, packet generator
├── crsf_rx.c             — CRSF receiver
├── crsf_tx.c             — CRSF transmitter
├── ibus_rx.c             — IBUS receiver
├── ibus_tx.c             — IBUS transmitter
├── joystick.c            — evdev joystick reader with CRSF / IBUS output
└── README.md             — This file
```

## License

Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
