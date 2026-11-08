## Tally Counter 1.0.1 — 2026-11-08

This patch adds a quiet Fine gears ratchet during reset: 24 teeth per knob turn, a fixed 70% reset-sound level, and a soft seating tick. The original tally click, interface, “Tally Counter” window title, counting behavior, and saved data remain unchanged. There are no additional sound settings.

The release contains five files: a portable Windows ZIP, a native Linux tarball, a Debian package, an AppImage, and the source archive. The Windows executable is at the ZIP root, the Linux executable is at the tarball root, and the Debian package installs the real application in `/usr/bin`.

The source archive includes corresponding third-party source and one `SHA256SUMS` file covering all four binary downloads. Extract that file alongside the downloaded packages and run `sha256sum --check SHA256SUMS` to check their integrity.

Windows builds target Windows 10/11 x64. Linux builds target Ubuntu 24.04 x64; see the repository README for package requirements and AppImage instructions. The AppImage supports extraction when FUSE is unavailable.
