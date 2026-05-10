# JoyCam

**JoyCam** — bridge your joystick, receiver, or transmitter to the CRSF
(Crossfire), IBUS (FlySky) and SBUS (Futaba) protocols on Linux.  
Receive RC channels and link telemetry from an ELRS / TBS Crossfire receiver,
generate IBUS or SBUS control packets, or read a USB joystick and map its axes
to channel values — all in one toolbox.

Optimised for embedded Linux and **OpenIPC-compatible devices** (IP cameras, NVRs
based on Sigmastar, Goke, Hisilicon SoCs). Run `crsf_rx`, `ibus_rx` or `sbus_rx`
directly on the camera's UART — no separate computer needed.

## Tools

| Tool | Description | OpenIPC |
|------|-------------|---------|
| `crsf_rx` | Reads CRSF frames from a serial port, parses 16 RC channels (11-bit) and link statistics (RSSI, LQ, RF power). | ✅ |
| `crsf_tx` | Generates CRSF RC channel packets at 100 Hz with a sawtooth sweep on a selected channel. | ✅ |
| `ibus_rx` | Reads IBUS (FlySky) frames from a serial port, parses 14 RC channels. | ✅ |
| `ibus_tx` | Generates IBUS RC channel packets at 50 Hz with a sawtooth sweep. | ✅ |
| `sbus_rx` | Reads SBUS (Futaba) frames from a serial port, parses 16 RC channels. Requires hardware inverter for USB-UART. | ✅ |
| `sbus_tx` | Generates SBUS RC channel packets at 100 Hz with a sawtooth sweep. Requires hardware inverter for USB-UART. | ✅ |
| `joystick` | Reads a USB joystick/gamepad via evdev and maps axis values to the CRSF (172–1811), SBUS (172–1811), or IBUS (1000–2000) range. Can transmit directly over UART or via RFC 2217 (serial-over-TCP). | ⚠️ needs USB host |

## Requirements

- Linux (tested on Ubuntu 22.04 / Debian 12, OpenIPC firmware)
- gcc with GNU99 support (or cross-compiler for target SoC)
- **No external libraries.** Pure POSIX + Linux ioctl (with POSIX sockets for RFC 2217).
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

Seven binaries are produced: `crsf_rx`, `crsf_tx`, `ibus_rx`, `ibus_tx`, `sbus_rx`, `sbus_tx`, `joystick`.

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

### Receive SBUS data

```bash
./sbus_rx /dev/ttyAMA0 [-d]
```

SBUS uses 100000 baud 8E2 with an inverted signal. On most SoC UARTs
the inversion is handled in hardware; USB-UART adapters typically need an
external inverter circuit.

Output:

```
Listening for SBUS data on /dev/ttyAMA0...
Channels: 0:992 1:992 2:992 3:992 4:992 5:992 6:992 7:992 8:992 9:992 10:992 11:992 12:992 13:992 14:992 15:992
```

### Transmit test frames

CRSF (100 Hz):

```bash
./crsf_tx /dev/ttyACM0 [-a <ch>]
```

IBUS (50 Hz):

```bash
./ibus_tx /dev/ttyACM0 [-a <ch>]
```

SBUS (100 Hz):

```bash
./sbus_tx /dev/ttyAMA0 [-a <ch>]
```

Channel 0 sweeps by default. Use `-a <ch>` to sweep a different channel.

### Read joystick / full-chain bridge

`joystick` reads a USB joystick via evdev and maps its axes/buttons to
channel values. It supports **CRSF** (`-p crsf`), **IBUS** (`-p ibus`),
and **SBUS** (`-p sbus`). Five display modes are available:

| Mode | Command | Behaviour |
|------|---------|-----------|
| Status line | `./joystick -p crsf\|ibus\|sbus <evdev>` | Show all channels every frame |
| Verbose | `./joystick -p crsf\|ibus\|sbus <evdev> -v` | Print every axis/button event + HEX dump |
| Transmit | `./joystick -p crsf\|ibus\|sbus <evdev> <serial>` | Send frames silently |
| Tx + status | `./joystick -p crsf\|ibus\|sbus <evdev> <serial> -d` | Send + status line |
| Tx + verbose | `./joystick -p crsf\|ibus\|sbus <evdev> <serial> -v` | Send + raw events |

Find your evdev device with:

```bash
ls -l /dev/input/by-id/*-joystick
```

Examples:

```bash
# Debug — see axis/button events (CRSF)
./joystick -p crsf /dev/input/by-id/usb-045e_028e-event-joystick

# Silent transmit — send IBUS over UART
./joystick -p ibus /dev/input/by-id/usb-045e_028e-event-joystick /dev/ttyS0

# Silent transmit — send SBUS over UART
./joystick -p sbus /dev/input/by-id/usb-045e_028e-event-joystick /dev/ttyAMA0

# Debug + transmit (CRSF)
./joystick -p crsf /dev/input/by-id/usb-045e_028e-event-joystick /dev/ttyS0 -d
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
# CRSF (100 Hz)
./crsf_tx /tmp/ttyV0

# or IBUS (50 Hz)
./ibus_tx /tmp/ttyV0

# or SBUS (100 Hz) — uses custom 100000 baud via termios2
./sbus_tx /tmp/ttyV0
```

In terminal **B** — receive and decode:

```bash
# CRSF
./crsf_rx /tmp/ttyV1

# or IBUS
./ibus_rx /tmp/ttyV1

# or SBUS
./sbus_rx /tmp/ttyV1
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
./joystick -p crsf /dev/input/by-id/usb-045e_028e-event-joystick /tmp/ttyV0 -d

# or IBUS
./joystick -p ibus /dev/input/by-id/usb-045e_028e-event-joystick /tmp/ttyV0 -d

# or SBUS
./joystick -p sbus /dev/input/by-id/usb-045e_028e-event-joystick /tmp/ttyV0 -d
```

3. **In terminal B** — receive frames:

```bash
# CRSF
./crsf_rx /tmp/ttyV1

# or IBUS
./ibus_rx /tmp/ttyV1

# or SBUS
./sbus_rx /tmp/ttyV1
```

Move joystick sticks — the receiver shows live channel values.

### Axis mapping (all protocols)

| evdev code | Meaning | CRSF channel | IBUS channel | SBUS channel |
|-----------:|---------|:------------:|:------------:|:------------:|
| 0 | Left stick X (LX) | ch0 | ch0 | ch0 |
| 1 | Left stick Y (LY) | ch1 | ch1 | ch1 |
| 2 | Right stick X (RX) | ch2 | ch2 | ch2 |
| 5 | Right stick Y (RY) | ch3 | ch3 | ch3 |
| 9 | Left trigger (LT) | ch4 | ch4 | ch4 |
| 10 | D-pad X axis | ch5 | ch5 | ch5 |
| 16 | D-pad Y axis | ch6 | ch6 | ch6 |
| 17 | Extra axis | ch7 | ch7 | ch7 |

### Button mapping (all protocols)

| evdev code | Linux name | CRSF channel | IBUS channel | SBUS channel |
|-----------:|------------|:------------:|:------------:|:------------:|
| 304 | `BTN_SOUTH` (A / cross) | ch8 | ch8 | ch8 |
| 305 | `BTN_EAST` (B / circle) | ch9 | ch9 | ch9 |
| 306 | `BTN_NORTH` (X / triangle) | ch10 | ch10 | ch10 |
| 307 | `BTN_WEST` (Y / square) | ch11 | ch11 | ch11 |
| 308 | `BTN_TL` (left bumper) | ch12 | ch12 | ch12 |
| 309 | `BTN_TR` (right bumper) | ch13 | ch13 | ch13 |
| 310 | `BTN_TL2` (left trigger) | ch14 | ch14 | ch14 |
| 311 | `BTN_TR2` (right trigger) | ch15 | ch15 | ch15 |
| 312 | `BTN_SELECT` (back) | ch16 | ch16 | ch16 |
| 313 | `BTN_START` (start) | ch17 | ch17 | ch17 |
| 314 | `BTN_THUMBL` (left stick click) | ch18 | ch18 | ch18 |
| 315 | `BTN_THUMBR` (right stick click) | ch19 | ch19 | ch19 |

All other buttons are ignored.

## RFC 2217 (Serial-over-TCP) support

All tools can connect to a remote serial port via **RFC 2217** (Telnet COM Port
Control Option) — just use a `tcp:host:port` URI instead of a device path:

```bash
# Connect to an RFC 2217 server instead of a local UART
./crsf_rx tcp:192.168.1.100:2217
./ibus_tx tcp:192.168.1.100:2217
./joystick -p crsf /dev/input/event0 tcp:192.168.1.100:2217 -d
```

The client automatically negotiates baud rate, data size, parity, and stop bits
using the Telnet COM-PORT-OPTION protocol. The `0xFF` byte is transparently
escaped/unescaped in the data stream. Modem and line state notifications
are logged at debug level.

### Testing with socat

Create a virtual RFC 2217 server that bridges to a local serial port:

```bash
socat TCP-LISTEN:2217,reuseaddr,fork FILE:/dev/ttyS0,raw,nonblock,waitlock=/tmp/s0.lock
```

Then connect from another machine:

```bash
./crsf_rx tcp:192.168.1.200:2217
```

### Testing without hardware (loopback)

```bash
# Terminal A — RFC 2217 server (dummy)
socat -d -d TCP-LISTEN:2217,reuseaddr pty,raw,echo=0,link=/tmp/ttyV0 &

# Terminal B — generate CRSF frames
./crsf_tx /tmp/ttyV0

# Terminal C — receive via RFC 2217
./crsf_rx tcp:127.0.0.1:2217
```

## Project structure

```
.
├── Makefile              — Build system
├── joycam.h              — Master header: CRSF + IBUS constants, structures, prototypes
├── joycam.c              — Shared utils: serial I/O, channel display, axis/button mapping
├── joycrsf.c             — CRSF protocol: CRC8, packet parser FSM, packet generator
├── joyibus.c             — IBUS protocol: checksum, packet parser FSM, packet generator
├── joysbus.c             — SBUS protocol: termios2 baudrate, packet parser FSM, packet generator
├── crsf_rx.c             — CRSF receiver
├── crsf_tx.c             — CRSF transmitter
├── ibus_rx.c             — IBUS receiver
├── ibus_tx.c             — IBUS transmitter
├── sbus_rx.c             — SBUS receiver
├── sbus_tx.c             — SBUS transmitter
├── joystick.c            — evdev joystick reader with CRSF / IBUS / SBUS output
└── README.md             — This file
```

## License

Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
