include ../config.mk

MAN = windowchef.1
MANUAL = "Windowchef Manual"

.POSIX:

page:
	ronn --manual=$(MANUAL) $(MAN).md

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
