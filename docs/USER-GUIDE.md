# Tally Counter user guide

[Back to README](../README.md) · [Build from source](BUILDING.md)

## Installation

Download a package from the [1.0.0 release](https://github.com/Davoda1/Tally-Counter/releases/tag/v1.0.0). Windows packages target Windows 10/11 x64. All supplied Linux packages target x86-64 and require glibc 2.38 or newer. On older Linux systems, build from source against your system's libraries.

### Windows ZIP

Extract `Tally-Counter-1.0.0-Windows-x64.zip` into a folder and open **TallyCounter.exe** at the archive root. The executable runs directly and needs no separate application runtime. Keep the accompanying license and notice files when redistributing it.

To update, close the app and extract the new version. The saved count lives in your user profile, so replacing the program does not reset it.

### Linux AppImage

The AppImage bundles the GUI libraries. Make it executable and run it:

```sh
chmod +x Tally-Counter-1.0.0-x86_64.AppImage
./Tally-Counter-1.0.0-x86_64.AppImage
```

You still need a graphical Linux session and the host's basic system libraries. The AppImage runtime normally uses FUSE. If your system does not provide compatible FUSE support, extract it into an empty working directory and run its native `AppRun` executable:

```sh
./Tally-Counter-1.0.0-x86_64.AppImage --appimage-extract
./squashfs-root/AppRun
```

Close the app before replacing or deleting the AppImage. Your saved count and window settings use the same location as the other Linux packages.

### Linux TAR.GZ

This archive contains the real `tally-counter` executable at its root. It uses the system's GTK 3.24 or newer, Cairo, Pango, GLib, ATK, and ALSA libraries. Library package names vary by distribution. On a Debian-compatible system, the DEB is the easiest way to let the package manager resolve these dependencies.

Extract the archive into a folder and run it:

```sh
mkdir -p tally-counter-1.0.0
tar -xzf Tally-Counter-1.0.0-Linux-x64.tar.gz -C tally-counter-1.0.0
./tally-counter-1.0.0/tally-counter
```

For a manual per-user installation, copy the executable into a directory on your `PATH`:

```sh
install -Dm755 tally-counter-1.0.0/tally-counter "$HOME/.local/bin/tally-counter"
```

Keep the archive's license and notice files if you redistribute the binary.

### Debian-compatible package

From the download folder, install the DEB with dependency resolution:

```sh
sudo apt install ./tally-counter_1.0.0_amd64.deb
```

Launch **Tally Counter** from your applications menu or run `tally-counter`. The package installs the native executable at `/usr/bin/tally-counter` and includes desktop icons.

Remove the package with:

```sh
sudo apt remove tally-counter
```

Removing the program leaves your per-user count and window settings in place.

### Verify a download

The source archive contains one `SHA256SUMS` file covering all four binary packages. Download the source archive and the binary packages you want to check into the same directory, then extract the source:

```sh
tar -xzf Tally-Counter-1.0.0-Source.tar.gz
```

On Linux, check downloaded packages from that directory:

```sh
sha256sum --ignore-missing -c Tally-Counter-1.0.0-Source/SHA256SUMS
```

On Windows, use PowerShell to calculate a downloaded file's hash, then compare it with its line in the extracted `SHA256SUMS` file:

```powershell
Get-FileHash .\Tally-Counter-1.0.0-Windows-x64.zip -Algorithm SHA256
```

## Counting

Left-click anywhere in the app's content except the reset control, or press Space while the window has focus. Each click adds one, and a double-click adds two. Holding Space adds only one; release it before the next tally. A quiet mechanical click confirms each successful increment.

The four digits always show a value from `0000` to `9999`. At the upper limit, further inputs leave the count unchanged and make no sound. The title bar and its window controls behave normally.

## Resetting

Click the reset knob, or press Tab to select Reset and then Enter. Space continues to add one even when Reset is selected.

Reset sets and saves the count to zero immediately. The knob turns clockwise while the digits roll backward using the same timing curve. The animation takes about 0.45 seconds at a low count, about one second at 1000, and at most 2.2 seconds at 9999. Both parts finish together.

If you add a tally while the digits are rewinding, they move to the new count immediately while the knob completes its rotation. Repeated resets at zero do not lengthen an active rotation. Reset is silent.

## Window and appearance

The first window opens at 560 × 280 logical pixels. You can resize it down to 400 × 220; the counter scales to fit. Normal window size and position, plus maximized state, are saved on close and restored on the next launch. A changed monitor setup is handled by adjusting the saved bounds to an available work area. Minimized state is not restored.

Position restoration works on Windows and X11. Wayland compositors decide where windows appear.

The app follows the Windows application light/dark preference or the current GTK theme on Linux. There is no separate theme switch inside the app. Sound follows the system's audio output and volume.

## Saved data

Tally Counter works offline. It has no account, telemetry, network features, or background service.

| Platform | Settings directory |
| --- | --- |
| Windows | `%APPDATA%\TallyCounter\` |
| Linux with `XDG_CONFIG_HOME` set | `$XDG_CONFIG_HOME/tally-counter/` |
| Linux otherwise | `~/.config/tally-counter/` |

The directory contains two small files:

| File | Purpose | Saved when |
| --- | --- | --- |
| `count.txt` | The current integer count | After each count change, including reset |
| `window.ini` | Normal window geometry and maximized state | When the app closes |

Count writes use an atomic file replacement. A missing, malformed, or out-of-range count starts at `0000`. Invalid window settings fall back to defaults.

To back up your count, close the app and copy `count.txt`. To reset only window placement, close the app and remove `window.ini`. To clear all saved data, close the app and remove its settings directory.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| “Count could not be saved” | The settings directory must be writable by your user. Check its permissions and available disk space; the displayed count may not survive a restart until saving works again. |
| Linux reports `GLIBC_2.38` is missing | Use a newer distribution or build from source on your current system. The AppImage does not replace the host's glibc. |
| The Linux TAR.GZ cannot find a shared library | Install your distribution's GTK 3, Cairo/Pango, and ALSA runtime packages, or use the DEB/AppImage. |
| The AppImage reports a FUSE error | Use the extraction method above, or install compatible FUSE support from your distribution. |
| The counter works but no click is heard | Check the system output, volume, and mute state. On Linux, check that ALSA and its desktop audio integration are available. Counting remains usable when audio is unavailable. |
| The window opens at a different position on Wayland | This is controlled by the compositor. Size and maximized state can still be restored. |
| Dark appearance does not follow a Linux desktop setting | Check the GTK 3 theme used by that desktop. The app follows GTK's theme settings. |

For a reproducible problem, [open a bug report](https://github.com/Davoda1/Tally-Counter/issues/new?template=bug_report.yml) with the app version, package type, operating system, and steps to reproduce it.
