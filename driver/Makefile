INSTALL		:= install
PYTHON		:= python3
RM		:= rm

PREFIX		:= /usr/local
DESTDIR		:=

all:
	@true

clean:
	@true

install:
	$(PYTHON) setup_driver.py install --prefix "$(DESTDIR)$(PREFIX)"
	$(RM) -rf build
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) linuxcnchal_cnccontrol.py $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) linuxcnchal_cnccontrol $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) -d $(DESTDIR)/etc/sudoers.d
	$(INSTALL) -g 0 -o 0 -m 0440 cnccontrol.sudo $(DESTDIR)/etc/sudoers.d/zzz-cnccontrol

.PHONY: all install clean
