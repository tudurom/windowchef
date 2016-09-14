include ../config.mk

MAN = windowchef.1 \
	  waitron.1
MANUAL = "Windowchef Manual"

.POSIX:

all: $(MAN)

%.1: *.1.md
	ronn --manual=$(MANUAL) $@.md

clean:
	rm *.1
	rm *.1.html

install: $(MAN)
	mkdir -p $(DESTDIR)$(MANDIR)/
	cp -f $(MAN) $(DESTDIR)$(MANDIR)/

uninstall:
	@echo "uninstalling manpages"
	@for page in $(MAN); do \
		rm -f $(DESTDIR)$(MANDIR)/$$page; \
	done
