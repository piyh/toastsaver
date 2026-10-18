# toastsaver - needs SDL2 2.0.18+ (Debian/Pi OS: sudo apt install libsdl2-dev)
CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra $(shell pkg-config --cflags sdl2)
LDLIBS  += $(shell pkg-config --libs sdl2) -lm
PREFIX  ?= /usr/local
SRC     := $(wildcard src/*.c src/savers/*.c)

bin/toastsaver: $(SRC) src/engine.h
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

install: bin/toastsaver
	install -Dm755 bin/toastsaver $(DESTDIR)$(PREFIX)/bin/toastsaver
	ln -sf toastsaver $(DESTDIR)$(PREFIX)/bin/toastsaver-picker
	ln -sf toastsaver $(DESTDIR)$(PREFIX)/bin/toastsaver-settings
	install -Dm755 scripts/toastsaver-idle $(DESTDIR)$(PREFIX)/bin/toastsaver-idle
	mkdir -p $(DESTDIR)$(PREFIX)/share/toastsaver
	bin/toastsaver --write-icon $(DESTDIR)$(PREFIX)/share/toastsaver/icon.png

clean:
	rm -rf bin

.PHONY: install clean
