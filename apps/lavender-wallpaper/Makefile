CFLAGS ?= -O2 -Wall -Wextra
GTK = $$(pkg-config --cflags --libs gtk4) -lm
PREFIX ?= $(HOME)/.local

all: lavwall lavshot
lavwall: lavwall.c lavrender.h
	$(CC) $(CFLAGS) -o $@ $< $(GTK)
lavshot: lavshot.c lavrender.h
	$(CC) $(CFLAGS) -o $@ $< $(GTK)
install: all
	install -Dm755 lavwall $(DESTDIR)$(PREFIX)/bin/lavwall
	install -Dm755 lavshot $(DESTDIR)$(PREFIX)/bin/lavshot
	install -Dm644 lavwall.desktop $(DESTDIR)$(PREFIX)/share/applications/lavwall.desktop
	install -Dm644 lavshot.desktop $(DESTDIR)$(PREFIX)/share/applications/lavshot.desktop
clean:
	rm -f lavwall lavshot
.PHONY: all install clean
