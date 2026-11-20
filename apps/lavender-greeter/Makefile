PKGS    = gtk4 json-glib-1.0 gio-unix-2.0
CFLAGS ?= -O2 -pipe
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))

lavender-greeter: lavender-greeter.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS) -s

clean:
	rm -f lavender-greeter
.PHONY: clean
