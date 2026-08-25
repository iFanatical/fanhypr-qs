# fanhypr-qs-shell — hand-rolled C++/Qt bar for Hyprland.
#
# Needs qtbase (widgets, gui, dbus, network), qtwayland (the platform plugin —
# without it Qt has no Wayland backend at all), layer-shell-qt (the
# wlr-layer-shell binding), and libpulse. No X11, no xcb: everything the dwm
# build used them for is a layer-shell request now.
#
# Gentoo:  emerge dev-qt/qtbase dev-qt/qtwayland kde-plasma/layer-shell-qt \
#                 media-sound/pulseaudio-daemon

QTMODS   = Qt6Widgets Qt6Gui Qt6Core Qt6DBus Qt6Network
PKGS     = $(QTMODS) libpulse

# layer-shell-qt ships CMake config files, not a .pc, so it is found by hand.
# Distros disagree about where the headers land; take the first that exists,
# and include its *parent* so `#include <LayerShellQt/Window>` resolves.
LSQT_DIR ?= $(firstword $(wildcard \
              /usr/include/LayerShellQt \
              /usr/include/qt6/LayerShellQt \
              /usr/local/include/LayerShellQt))
LSQT_CFLAGS = $(if $(LSQT_DIR),-I$(patsubst %/,%,$(dir $(LSQT_DIR))))
LSQT_LIBS  ?= -lLayerShellQtInterface

CXX      ?= g++
MOC      ?= $(shell pkg-config --variable=libexecdir Qt6Core)/moc
CXXFLAGS += -std=c++17 -O2 -Wall -fPIC $(shell pkg-config --cflags $(PKGS)) \
            $(LSQT_CFLAGS)
LDLIBS    = $(shell pkg-config --libs $(PKGS)) $(LSQT_LIBS)

MODULES  = widgets popup wlutil procutil hypripc hyprstate calendar pills \
           panel network bluetooth volume tray traymenu desktopentry \
           launcher ipc clipboard system battery wallpaper notification
# headers containing Q_OBJECT classes
MOCHDRS  = widgets popup procutil hypripc hyprstate calendar pills panel \
           network bluetooth volume tray traymenu desktopentry launcher ipc \
           clipboard system battery wallpaper
MOCHDRS += notification

OBJ      = $(MODULES:%=%.o) main.o $(MOCHDRS:%=moc_%.o)

all: fanhypr-qs-shell

fanhypr-qs-shell: $(OBJ)
	$(CXX) -o $@ $(OBJ) $(LDLIBS)

moc_%.cpp: %.h
	$(MOC) $< -o $@

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(OBJ): $(wildcard *.h)

clean:
	rm -f fanhypr-qs-shell *.o moc_*.cpp

.PHONY: all clean
