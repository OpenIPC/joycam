
## Relay

```
socat -d -d pty,raw,echo=0,link=/tmp/ttyV0 pty,raw,echo=0,link=/tmp/ttyV1
```

## RFC 2217 (serial-over-TCP) loopback

```
# Create a virtual serial pair
socat -d -d pty,raw,echo=0,link=/tmp/ttyV0 pty,raw,echo=0,link=/tmp/ttyV1

# Bridge one end to TCP via socat (RFC 2217-compatible server)
socat TCP-LISTEN:2217,reuseaddr,fork FILE:/tmp/ttyV0,raw,nonblock

# In another terminal, connect via RFC 2217
./crsf_rx tcp:127.0.0.1:2217
```

## Usage via TCP (`tcp:host:port` URI)

All tools support RFC 2217 URIs:

```bash
# Receiver
./crsf_rx tcp:192.168.1.100:2217

# Transmitter
./crsf_tx tcp:192.168.1.100:2217

# Joystick
./joystick -p crsf /dev/input/event0 tcp:192.168.1.100:2217 -d
```




## Receiver CRSF

```
./crsf_rx /tmp/ttyV1
Listening for CRSF data on /tmp/ttyV1...
```

## Transmitter CRSF

```
./crsf_tx /tmp/ttyV0
Sending CRSF frames to /tmp/ttyV0... sweep on ch0
```

## Joystic CRSF

```
./joystick -p crsf -d /dev/input/by-id/usb-shanwan_Android_GamePad-event-joystick /tmp/ttyV0
```




## Receiver IBUS

```
./ibus_rx /tmp/ttyV1
Listening for IBUS data on /tmp/ttyV1...
```

## Transmitter IBUS

```
./ibus_tx /tmp/ttyV0
Sending IBUS frames to /tmp/ttyV0... sweep on ch0
```

## Joystic IBUS

```
./joystick -p ibus -d /dev/input/by-id/usb-shanwan_Android_GamePad-event-joystick /tmp/ttyV0
```




## Receiver SBUS

```
./sbus_rx /tmp/ttyV1
Listening for SBUS data on /tmp/ttyV1...
```

## Transmitter SBUS

```
./sbus_tx /tmp/ttyV0
Sending SBUS frames to /tmp/ttyV0... sweep on ch0
```

## Joystic SBUS

```
./joystick -p sbus -d /dev/input/by-id/usb-shanwan_Android_GamePad-event-joystick /tmp/ttyV0
```


