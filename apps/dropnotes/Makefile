# gtk4-layer-shell должен идти до wayland-client при линковке — pkg-config ставит его первым
PKGS    = gtk4-layer-shell-0 gtk4 json-glib-1.0
CFLAGS ?= -O2 -pipe
CFLAGS += -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS)) -lm
PREFIX ?= $(HOME)/.local

dropnotes: dropnotes.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS) -s

install: dropnotes
	install -Dm755 dropnotes $(PREFIX)/bin/dropnotes

clean:
	rm -f dropnotes
.PHONY: install clean
