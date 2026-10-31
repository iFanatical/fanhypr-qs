# fanhypr-qs — the Hyprland session for fanshell-qs: Hyprland's config and
# the portal choice. The shell itself is fanshell-qs, installed separately.
#
#   make link-config  link config/ (hypr/, hyprland-portals.conf) into $HOME

all:
	@echo 'nothing to build; run make link-config (and build fanshell-qs)'

# Per user, so never run as root: it links into $HOME.
link-config:
	./tools/link-config

.PHONY: all link-config
