#
# Copyright (c) OpenIPC  https://openipc.org  MIT License
#
# CRSF Linux Bridge — Makefile
#

CROSS		?=
CC		:= $(CROSS)gcc
PKG_CONFIG	?= pkg-config
VERSION		:= 1.0.0
CFLAGS		:= -std=gnu99 -D_GNU_SOURCE -Os
CFLAGS		+= -DVERSION=\"$(VERSION)\"
CFLAGS		+= -Wall -Wextra -Werror=implicit-function-declaration
CFLAGS		+= -ffunction-sections -fdata-sections
CFLAGS		+= $(shell $(PKG_CONFIG) --cflags libevdev)
LDFLAGS		:= -Wl,--gc-sections -s

CRSF_OBJS	:= JoyCRSF.o

.PHONY: all clean

all: crsf_rx crsf_tx joystick

# --- CRSF receiver ---
crsf_rx: crsf_rx.o $(CRSF_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell $(PKG_CONFIG) --libs libserialport)

crsf_rx.o: crsf_rx.c JoyCRSF.h
	$(CC) $(CFLAGS) -c -o $@ $<

# --- CRSF transmitter ---
crsf_tx: crsf_tx.o $(CRSF_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell $(PKG_CONFIG) --libs libserialport)

crsf_tx.o: crsf_tx.c JoyCRSF.h
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Joystick ---
joystick: joystick.o $(CRSF_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell $(PKG_CONFIG) --libs libserialport) $(shell $(PKG_CONFIG) --libs libevdev)

joystick.o: joystick.c JoyCRSF.h
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Shared JoyCRSF library ---
JoyCRSF.o: JoyCRSF.c JoyCRSF.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o crsf_rx crsf_tx joystick
