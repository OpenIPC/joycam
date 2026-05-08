#
# Copyright (c) OpenIPC  https://openipc.org  The Prosperity Public License 3.0.0
#
# JoyCam — Makefile
#

CROSS		?=
CC		:= $(CROSS)gcc
STRIP		:= $(CROSS)strip
VERSION		:= 1.3.0
CFLAGS		:= -std=gnu99 -D_GNU_SOURCE -Os
CFLAGS		+= -DVERSION=\"$(VERSION)\"
CFLAGS		+= -Wall -Wextra -Werror=implicit-function-declaration -Wunused-result
CFLAGS		+= -ffunction-sections -fdata-sections
LDFLAGS		:= -Wl,--gc-sections

JOYCAM_OBJS	:= joycam.o joycrsf.o joyibus.o

DESTDIR		?=
.PHONY: all clean install

all: crsf_rx crsf_tx ibus_rx ibus_tx joystick

# --- CRSF receiver ---
crsf_rx: crsf_rx.o $(JOYCAM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	$(STRIP) --strip-all $@

crsf_rx.o: crsf_rx.c joycam.h
	$(CC) $(CFLAGS) -c -o $@ $<

# --- CRSF transmitter ---
crsf_tx: crsf_tx.o $(JOYCAM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	$(STRIP) --strip-all $@

crsf_tx.o: crsf_tx.c joycam.h
	$(CC) $(CFLAGS) -c -o $@ $<

# --- IBUS receiver ---
ibus_rx: ibus_rx.o $(JOYCAM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	$(STRIP) --strip-all $@

ibus_rx.o: ibus_rx.c joycam.h
	$(CC) $(CFLAGS) -c -o $@ $<

# --- IBUS transmitter ---
ibus_tx: ibus_tx.o $(JOYCAM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	$(STRIP) --strip-all $@

ibus_tx.o: ibus_tx.c joycam.h
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Joystick ---
joystick: joystick.o $(JOYCAM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	$(STRIP) --strip-all $@

joystick.o: joystick.c joycam.h
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Shared objects ---
joycam.o: joycam.c joycam.h
	$(CC) $(CFLAGS) -c -o $@ $<

joycrsf.o: joycrsf.c joycam.h
	$(CC) $(CFLAGS) -c -o $@ $<

joyibus.o: joyibus.c joycam.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: crsf_rx crsf_tx ibus_rx ibus_tx joystick
	mkdir -p $(DESTDIR)/usr/local/bin
	install -m 0755 crsf_rx $(DESTDIR)/usr/local/bin/
	install -m 0755 crsf_tx $(DESTDIR)/usr/local/bin/
	install -m 0755 ibus_rx $(DESTDIR)/usr/local/bin/
	install -m 0755 ibus_tx $(DESTDIR)/usr/local/bin/
	install -m 0755 joystick $(DESTDIR)/usr/local/bin/

clean:
	rm -f *.o crsf_rx crsf_tx ibus_rx ibus_tx joystick
