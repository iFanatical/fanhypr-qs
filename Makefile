# fanhypr-qs — a status bar for Hyprland.
#
# There is no window manager in this tree: Hyprland is the WM, and this builds
# only the bar (shell/) plus the helper scripts it shells out to.

PREFIX ?= /usr/local
BINDIR  = $(DESTDIR)$(PREFIX)/bin

SCRIPTS = $(wildcard scripts/fanhypr-qs-*)

all: shell/fanhypr-qs-shell

shell/fanhypr-qs-shell:
	$(MAKE) -C shell

# The helper scripts must be on the PATH that fanhypr-qs-shell inherits: the
# widgets exec them by bare name, and a missing one just makes that widget
# quietly show defaults. Installing them alongside the binary is what
# guarantees that.
install: all
	install -d $(BINDIR)
	install -m 755 shell/fanhypr-qs-shell $(BINDIR)/fanhypr-qs-shell
	install -m 755 $(SCRIPTS) $(BINDIR)

uninstall:
	rm -f $(BINDIR)/fanhypr-qs-shell
	for s in $(notdir $(SCRIPTS)); do rm -f $(BINDIR)/$$s; done

clean:
	$(MAKE) -C shell clean

.PHONY: all install uninstall clean shell/fanhypr-qs-shell
