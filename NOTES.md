
## Relay

```
socat -d -d pty,raw,echo=0,link=/tmp/ttyV0 pty,raw,echo=0,link=/tmp/ttyV1
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
