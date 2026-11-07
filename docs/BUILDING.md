# Building and packaging

[Back to README](../README.md) · [User guide](USER-GUIDE.md)

Tally Counter is a C++17 application. `src/ui.cpp` contains the shared drawing and animation logic, `src/counter.hpp` contains the count model, and the two platform frontends handle native input, persistence, window management, themes, and audio.

| Platform | Frontend and rendering | Build output |
| --- | --- | --- |
| Linux | GTK 3, Cairo, and Pango | `build/tally-counter` |
| Windows | Win32 and GDI+ | `build/TallyCounter.exe` |

## Linux

Install a C++17 compiler, GNU Make, `pkg-config`, and GTK 3 development headers. On a Debian-compatible build host:

```sh
sudo apt install build-essential pkg-config libgtk-3-dev
make
./build/tally-counter
```

The native Linux executable links to system GTK 3 and its dependencies. Click audio uses the system ALSA library, loaded at runtime. Missing audio support does not prevent counting. The release executable has its compiler runtimes linked statically; its required glibc version is still determined by the build system.

The distributed 1.0.0 Linux binaries require glibc 2.38 or newer. A local build against an older system can target that system, provided it has compatible GTK 3 development libraries and a C++17 toolchain.

For a system installation:

```sh
sudo make install
```

For a per-user installation:

```sh
make install PREFIX="$HOME/.local"
```

The install target copies the executable, desktop entry, icons, and license notices. Ensure the selected prefix's `bin` directory is on your `PATH`. `DESTDIR` can stage an installation for packaging.

## Windows cross-build

Use an x86-64 MinGW-w64 C++ toolchain on a Linux build host. On Debian-compatible systems:

```sh
sudo apt install g++-mingw-w64-x86-64
make windows
```

The result is a Windows x64 GUI executable. It uses Windows system libraries and statically links the compiler runtime, so end users do not need a separate MinGW installation. The included manifest and resources provide the native icon and DPI configuration.

You can override the toolchain with `WINCXX` and `WINDRES`. `CXX`, `CXXFLAGS`, and `PKG_CONFIG` configure the Linux build. For example:

```sh
make CXX=clang++
```

Changing compiler flags does not automatically invalidate an existing build. Run `make clean` before rebuilding with a different compiler or incompatible flags.

## Shared tests

The shared behavioral tests need only a C++17 compiler and GNU Make:

```sh
make test
```

The tests exercise persisted values from 0 through 9999, invalid input, formatting and carries, the maximum count, rolling-digit timing, synchronized reset, input during rewind, and the embedded click format and level.

For changes to a platform frontend, also launch that native app and check the affected behavior. Useful manual checks include rapid clicks, Space held down, reset focus and Enter, counting during reset, closing and reopening, changing themes, and restoring a maximized window. Unit tests do not exercise native event delivery or an operating system's window manager.

## Embedded assets

The reset icon is stored as `assets/reset.svg` and embedded as vector drawing commands in `src/reset_svg.hpp`. After changing the SVG, regenerate the header:

```sh
python3 packaging/embed_reset_svg.py
```

The original short click is generated into `assets/tally-click.wav` and `src/click_sound.hpp`:

```sh
python3 packaging/make_click_sound.py
```

Generated files are included so an ordinary build can use them directly. Make regenerates them when their inputs are newer. Python 3 is required for regeneration and packaging, not for running the app. Commit generated asset changes alongside their source changes.

## Release packaging

Package on an x86-64 Linux build host with Python 3, `dpkg-deb`, `strip`, and a C compiler available. The AppImage builder collects the host's GTK libraries and data, ALSA including its PulseAudio plugins, and DejaVu fonts. On Ubuntu 24.04, install these packaging inputs in addition to the build dependencies above:

```sh
sudo apt install python3 dpkg-dev libasound2-plugins libgdk-pixbuf2.0-bin fonts-dejavu-core adwaita-icon-theme
```

Build both executables and run the tests before packaging:

```sh
make
make windows
make test
python3 packaging/package.py --output dist
```

The AppImage build downloads pinned AppImage tools and verifies their SHA-256 hashes before use. It bundles GUI dependencies while leaving glibc and the host's display/audio services to the system. Building on a newer distribution may raise the resulting package's minimum system requirements.

The release packager creates five assets:

| File | Contents |
| --- | --- |
| `Tally-Counter-1.0.0-Windows-x64.zip` | `TallyCounter.exe` at the archive root, usage notes, and license notices |
| `Tally-Counter-1.0.0-Linux-x64.tar.gz` | The native `tally-counter` executable at the archive root, usage notes, and license notices |
| `Tally-Counter-1.0.0-x86_64.AppImage` | The application and bundled GUI libraries in an executable AppImage |
| `tally-counter_1.0.0_amd64.deb` | The native executable, desktop integration, documentation, and license notices |
| `Tally-Counter-1.0.0-Source.tar.gz` | Source, build scripts, documentation, tests, license notices, and `SHA256SUMS` |

The source archive's single `SHA256SUMS` file covers the four binary packages. It is intentionally inside the source archive. Executables launch directly; no shell, batch, or PowerShell launcher is needed.

See `packaging/package.py --help` for packaging inputs and output-directory options, and [VERIFICATION.txt](../VERIFICATION.txt) for the checks recorded for this release. Inspect archive contents and test the produced packages before attaching them to a release.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/` | Shared model, drawing, animation, and native platform integrations |
| `assets/` | App icons, the reset SVG, and generated click audio |
| `tests/` | Shared behavioral tests |
| `packaging/` | Native resources, desktop entry, asset generators, and release packaging |
| `docs/` | User guide, build instructions, and README artwork/screenshots |
| `licenses/` | License texts and notices for distributed third-party components |

First-party material uses the [zlib License](../LICENSE). The [third-party notices](../THIRD-PARTY-NOTICES.txt) document the separate terms that apply to bundled components.
