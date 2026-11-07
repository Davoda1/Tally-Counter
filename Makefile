# Copyright (c) 2026 davoda1
# SPDX-License-Identifier: Zlib
CXX ?= g++
PKG_CONFIG ?= pkg-config
WINCXX ?= x86_64-w64-mingw32-g++
WINDRES ?= x86_64-w64-mingw32-windres
CXXFLAGS ?= -O2 -Wall -Wextra -Wpedantic
COMMON = -std=c++17 -Isrc
HEADERS = src/counter.hpp src/paint.hpp src/ui.hpp src/reset_svg.hpp src/click_sound.hpp
PREFIX ?= /usr

.PHONY: all windows test install clean package
all: build/tally-counter

build:
	mkdir -p build

src/reset_svg.hpp: assets/reset.svg packaging/embed_reset_svg.py
	python3 packaging/embed_reset_svg.py

src/click_sound.hpp: packaging/make_click_sound.py
	python3 packaging/make_click_sound.py

build/tally-counter: src/linux.cpp src/linux_audio.hpp src/ui.cpp $(HEADERS) | build
	$(CXX) $(COMMON) $(CXXFLAGS) -pthread -fstack-protector-strong -D_FORTIFY_SOURCE=2 -static-libgcc -static-libstdc++ src/linux.cpp src/ui.cpp -Wl,-z,relro,-z,now -o $@ -ldl $$($(PKG_CONFIG) --cflags --libs gtk+-3.0)

build/windows-res.o: packaging/windows.rc packaging/windows.manifest assets/icon.ico | build
	$(WINDRES) --preprocessor=$(WINCXX) --preprocessor-arg=-E --preprocessor-arg=-xc++ --preprocessor-arg=-DRC_INVOKED packaging/windows.rc -O coff -o $@

build/TallyCounter.exe: src/windows.cpp src/ui.cpp $(HEADERS) build/windows-res.o | build
	$(WINCXX) $(COMMON) $(CXXFLAGS) -s -static -static-libgcc -static-libstdc++ -municode -mwindows src/windows.cpp src/ui.cpp build/windows-res.o -o $@ -lgdiplus -lshell32 -lole32 -ladvapi32 -ldwmapi -luuid -lwinmm

windows: build/TallyCounter.exe

build/core-tests: tests/core.cpp src/ui.cpp $(HEADERS) | build
	$(CXX) $(COMMON) $(CXXFLAGS) tests/core.cpp src/ui.cpp -o $@

test: build/core-tests
	./build/core-tests

install: build/tally-counter
	install -Dm755 build/tally-counter $(DESTDIR)$(PREFIX)/bin/tally-counter
	install -Dm644 packaging/tally-counter.desktop $(DESTDIR)$(PREFIX)/share/applications/tally-counter.desktop
	install -Dm644 assets/icon.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/tally-counter.svg
	install -Dm644 assets/icon.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/tally-counter.png
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/doc/tally-counter/LICENSE
	install -Dm644 THIRD-PARTY-NOTICES.txt $(DESTDIR)$(PREFIX)/share/doc/tally-counter/THIRD-PARTY-NOTICES.txt
	install -d $(DESTDIR)$(PREFIX)/share/doc/tally-counter/licenses
	install -m644 licenses/*.txt $(DESTDIR)$(PREFIX)/share/doc/tally-counter/licenses/

package:
	python3 packaging/package.py

clean:
	rm -rf build
