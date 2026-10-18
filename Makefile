# toastsaver - needs SDL2 2.0.18+ (Debian/Pi OS: sudo apt install libsdl2-dev)
#   make && sudo make install                 installs under /usr/local
#   make install PREFIX=/usr DESTDIR=...      what the Debian package does
CC         ?= cc
CFLAGS     ?= -O2
PREFIX     ?= /usr/local
SYSCONFDIR ?= /etc
BINDIR     := $(PREFIX)/bin
DATADIR    := $(PREFIX)/share
CFLAGS     += -std=c99 -Wall -Wextra $(shell pkg-config --cflags sdl2) \
              -DTOASTSAVER_BINDIR=\"$(BINDIR)\" -DTOASTSAVER_DATADIR=\"$(DATADIR)\"
LDLIBS     += $(shell pkg-config --libs sdl2) -lm
SRC        := $(wildcard src/*.c src/savers/*.c)
SUBST      := sed 's|@BINDIR@|$(BINDIR)|g'

all: bin/toastsaver bin/toastsaver.png

bin/toastsaver: $(SRC) src/engine.h
	mkdir -p bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC) $(LDLIBS)

bin/toastsaver.png: bin/toastsaver
	bin/toastsaver --write-icon $@

install: all
	install -Dm755 bin/toastsaver $(DESTDIR)$(BINDIR)/toastsaver
	ln -sf toastsaver $(DESTDIR)$(BINDIR)/toastsaver-picker
	ln -sf toastsaver $(DESTDIR)$(BINDIR)/toastsaver-settings
	mkdir -p $(DESTDIR)$(DATADIR)/applications $(DESTDIR)$(SYSCONFDIR)/xdg/autostart
	$(SUBST) scripts/toastsaver-idle > $(DESTDIR)$(BINDIR)/toastsaver-idle
	chmod 755 $(DESTDIR)$(BINDIR)/toastsaver-idle
	$(SUBST) data/toastsaver-autostart.desktop > $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/toastsaver.desktop
	$(SUBST) data/toastsaver-picker.desktop > $(DESTDIR)$(DATADIR)/applications/toastsaver-picker.desktop
	$(SUBST) data/toastsaver-settings.desktop > $(DESTDIR)$(DATADIR)/applications/toastsaver-settings.desktop
	install -Dm644 bin/toastsaver.png $(DESTDIR)$(DATADIR)/icons/hicolor/128x128/apps/toastsaver.png
	install -Dm644 man/toastsaver.1 $(DESTDIR)$(DATADIR)/man/man1/toastsaver.1
	for m in toastsaver-picker toastsaver-settings toastsaver-idle; do \
	    ln -sf toastsaver.1 $(DESTDIR)$(DATADIR)/man/man1/$$m.1; done

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/toastsaver $(DESTDIR)$(BINDIR)/toastsaver-picker \
	      $(DESTDIR)$(BINDIR)/toastsaver-settings $(DESTDIR)$(BINDIR)/toastsaver-idle \
	      $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/toastsaver.desktop \
	      $(DESTDIR)$(DATADIR)/applications/toastsaver-picker.desktop \
	      $(DESTDIR)$(DATADIR)/applications/toastsaver-settings.desktop \
	      $(DESTDIR)$(DATADIR)/icons/hicolor/128x128/apps/toastsaver.png \
	      $(DESTDIR)$(DATADIR)/man/man1/toastsaver.1 \
	      $(DESTDIR)$(DATADIR)/man/man1/toastsaver-picker.1 \
	      $(DESTDIR)$(DATADIR)/man/man1/toastsaver-settings.1 \
	      $(DESTDIR)$(DATADIR)/man/man1/toastsaver-idle.1

clean:
	rm -rf bin

.PHONY: all install uninstall clean
