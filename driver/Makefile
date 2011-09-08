INSTALL		:= install
PREFIX		:= /usr/local
DESTDIR		:=

all:
	@true

clean:
	@true

install:
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) emc2hal_cnccontrol.py $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) emc2hal_cnccontrol $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) -m 0644 cnccontrol_driver.py $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) -d $(DESTDIR)/etc/sudoers.d
	$(INSTALL) -g 0 -o 0 -m 0440 cnccontrol.sudo $(DESTDIR)/etc/sudoers.d/zzz-cnccontrol

.PHONY: all install clean
