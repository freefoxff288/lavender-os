CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter
PKGS = gtk4 json-glib-1.0
PREFIX ?= /usr/local

all: lavender-installer

lavender-installer: lavender-installer.c
	$(CC) $(CFLAGS) $$(pkg-config --cflags $(PKGS)) -o $@ $< $$(pkg-config --libs $(PKGS)) -lm

install: lavender-installer
	install -Dm755 lavender-installer $(DESTDIR)$(PREFIX)/bin/lavender-installer

clean:
	rm -f lavender-installer
