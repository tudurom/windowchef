PREFIX = /usr/local
MANPREFIX = /usr/local/share/man
MANDIR = $(MANPREFIX)/man1

CFLAGS += -std=c99 -Wextra -O2
LDFLAGS += -lxcb -lxcb-ewmh -lxcb-icccm -lxcb-randr
