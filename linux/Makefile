# Makefile for the Jolimark FP-620K+ CUPS driver
#
# Build:
#   make
#
# Install (normally run through ./install.sh, which asks for the password):
#   sudo make install
#
# Override the install locations if your distribution uses different paths:
#   make install FILTERDIR=/usr/lib/cups/filter BACKENDDIR=/usr/lib/cups/backend

CC		?= cc
CFLAGS		?= -O2 -g -Wall -Wextra
LDFLAGS		?=
CUPS_CFLAGS	:= $(shell cups-config --cflags 2>/dev/null)
CUPS_LIBS	:= $(shell cups-config --libs 2>/dev/null)
CUPS_SERVERBIN	:= $(shell cups-config --serverbin 2>/dev/null)

FILTERDIR	?= $(if $(CUPS_SERVERBIN),$(CUPS_SERVERBIN)/filter,/usr/lib/cups/filter)
BACKENDDIR	?= $(if $(CUPS_SERVERBIN),$(CUPS_SERVERBIN)/backend,/usr/lib/cups/backend)
PPDDIR		?= /usr/share/cups/model

PROGRAMS	= rastertofp620kp fp620kp

all: $(PROGRAMS)

rastertofp620kp: src/rastertofp620kp.c
	$(CC) $(CFLAGS) $(CUPS_CFLAGS) -o $@ $< $(LDFLAGS) $(CUPS_LIBS) -lm

fp620kp: src/fp620kp-backend.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: all
	install -d "$(DESTDIR)$(FILTERDIR)" "$(DESTDIR)$(BACKENDDIR)" "$(DESTDIR)$(PPDDIR)"
	install -m 755 rastertofp620kp "$(DESTDIR)$(FILTERDIR)/rastertofp620kp"
	install -m 700 fp620kp "$(DESTDIR)$(BACKENDDIR)/fp620kp"
	install -m 644 jolimark-fp620kp.ppd "$(DESTDIR)$(PPDDIR)/jolimark-fp620kp.ppd"

uninstall:
	rm -f "$(DESTDIR)$(FILTERDIR)/rastertofp620kp"
	rm -f "$(DESTDIR)$(BACKENDDIR)/fp620kp"
	rm -f "$(DESTDIR)$(PPDDIR)/jolimark-fp620kp.ppd"

deb:
	dpkg-buildpackage -us -uc -b

release:
	./release.sh

clean:
	rm -f $(PROGRAMS) src/*.o

.PHONY: all install uninstall deb release clean
