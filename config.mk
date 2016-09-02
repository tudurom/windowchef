PREFIX = /usr/local
MANPREFIX = /usr/local/share
MANDIR = $(MANPREFIX)/man/man1

CFLAGS += -std=c99 -Wextra -g
LDFLAGS += -lxcb -lxcb-ewmh -lxcb-icccm -lxcb-randr
