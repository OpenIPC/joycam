# JoyCRSF

**JoyCRSF** — bridge your joystick, receiver, or transmitter to the CRSF
(Crossfire) protocol on Linux.  
Receive RC channels and link telemetry from an ELRS / TBS Crossfire receiver,
generate control packets, or read a USB joystick and map its axes to CRSF channel values — all in one toolbox.

Optimised for embedded Linux and **OpenIPC-compatible devices** (IP cameras, NVRs
based on Sigmastar, Goke, Hisilicon SoCs). Run `crsf_rx` directly on the camera's
UART — no separate computer needed.

## Tools

| Tool | Description | OpenIPC |
|------|-------------|---------|
| `crsf_rx` | Reads CRSF frames from a serial port, parses 16 RC channels (11-bit) and link statistics (RSSI, LQ, RF power). | ✅ |
| `crsf_tx` | Generates CRSF RC channel packets at 100 Hz with a sawtooth sweep on channel 0. | ✅ |
| `joystick` | Reads a USB joystick/gamepad via evdev and maps axis values to the CRSF range (172–1811). Can transmit directly over UART. | ⚠️ needs USB host |

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

Three binaries are produced: `crsf_rx`, `crsf_tx`, `joystick`.

## Usage

### Receive CRSF data

```bash
./crsf_rx /dev/ttyUSB0
```

Output:

```
Listening for CRSF data on /dev/ttyUSB0...
Channels: 0:992 1:1020 2:988 3:1700 4:992 5:992 6:992 7:992 8:992 9:992 10:992 11:992 12:992 13:992 14:992 15:992
Link Quality: 99%  RSSI1: -45 RSSI2: -48 Power: 100
```

On OpenIPC cameras the ELRS receiver is typically connected to the camera's
UART1 or UART2 (e.g. `/dev/ttyAMA1`).

### Transmit test frames

```bash
./crsf_tx /dev/ttyACM0
```

Sends RC channel frames at 100 Hz. Channel 0 sweeps from 172 to 1811.

### Read joystick / full-chain CRSF bridge

`joystick` reads a USB joystick via evdev and maps its axes/buttons to
CRSF channel values. It has four display modes:

| Mode | Command | Behaviour |
|------|---------|-----------|
| Status line | `./joystick <evdev>` | Show all 16 channels every frame |
| Verbose | `./joystick <evdev> -v` | Print every axis/button event + CRSF HEX dump |
| Transmit | `./joystick <evdev> <serial>` | Send CRSF frames silently |
| Tx + status | `./joystick <evdev> <serial> -d` | Send + status line |
| Tx + verbose | `./joystick <evdev> <serial> -v` | Send + raw events |

Find your evdev device with:

```bash
ls -l /dev/input/by-id/*-joystick
```

Examples:

```bash
# Debug — see axis/button events
./joystick /dev/input/by-id/usb-045e_028e-event-joystick

# Silent transmit — send CRSF over UART
./joystick /dev/input/by-id/usb-045e_028e-event-joystick /dev/ttyS0

# Debug + transmit
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
./crsf_tx /tmp/ttyV0
```

In terminal **B** — receive and decode:

```bash
./crsf_rx /tmp/ttyV1
```

You should see channels and link statistics appearing on the receiver side.

### Full chain: joystick → receiver (loopback)

`joystick` can read from evdev **and** write CRSF frames directly to a serial
port. Five operating modes are available:

| Mode | Command | Behaviour |
|------|---------|-----------|
| Status line | `./joystick <evdev>` | Show all 16 channels every frame |
| Verbose | `./joystick <evdev> -v` | Every axis/button event + CRSF HEX dump |
| Transmit | `./joystick <evdev> <serial>` | Send CRSF frames silently |
| Tx + status | `./joystick <evdev> <serial> -d` | Send + status line |
| Tx + verbose | `./joystick <evdev> <serial> -v` | Send + raw events |

Example loopback test using a virtual serial port:

1. **Create a virtual serial pair:**

```bash
socat -d -d pty,raw,echo=0,link=/tmp/ttyV0 \
            pty,raw,echo=0,link=/tmp/ttyV1
```

2. **In terminal A** — run the joystick bridge (transmitting to `/tmp/ttyV0`):

```bash
./joystick /dev/input/by-id/usb-045e_028e-event-joystick /tmp/ttyV0 -d
```

3. **In terminal B** — receive CRSF frames:

```bash
./crsf_rx /tmp/ttyV1
```

Move joystick sticks — the receiver shows live channel values:

```
Listening for CRSF data on /tmp/ttyV1...
Channels: 0:172  1:992  2:1811  3:992  4:992  5:992  6:992  7:992   | 8:992  9:992  10:992  11:992  12:992  13:992  14:992  15:992
Channels: 0:988  1:1020  2:1700  3:1500  4:992  5:992  6:992  7:992   | 8:992  9:992  10:992  11:992  12:992  13:992  14:992  15:992
```

**Axis mapping:** axes are mapped by evdev code (0→LX, 1→LY, 2→RX, 5→RY,
9→LT, 10→D-pad X, 16→D-pad Y, 17→extra).  
**Button mapping:** BTN_SOUTH(304)→ch8, BTN_EAST(305)→ch9, ... BTN_TL2(315)→ch19.
Unmapped buttons are ignored.

## Project structure

```
.
├── Makefile              — Build system
├── joycrsf.h             — CRSF protocol constants, structures, helpers, prototypes
├── joycrsf.c             — CRC8, packet parser FSM, packet generator, serial helpers
├── crsf_rx.c             — Serial receiver (uses crsf_serial_open)
├── crsf_tx.c             — Packet transmitter (uses crsf_serial_open)
├── joystick.c            — evdev joystick reader with CRSF output
└── README.md             — This file
```

## License

Copyright (c) OpenIPC  https://openipc.org  MIT License
