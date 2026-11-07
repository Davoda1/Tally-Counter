## Tally Counter 1.0.0

A small native four-digit tally counter for Windows and GNU/Linux. Click or press Space to count, use the reset knob or Tab then Enter to reset, and continue from the saved count next time.

The release contains five files: a portable Windows ZIP, a native Linux tarball, a Debian package, an AppImage, and the source archive. The Windows executable is at the ZIP root, the Linux executable is at the tarball root, and the Debian package installs the real application in `/usr/bin`.

The source archive includes one `SHA256SUMS` file covering all four binary downloads. Extract that file alongside the downloaded packages and run `sha256sum --check SHA256SUMS` to check their integrity.

Windows builds target Windows 10/11 x64. Linux builds target Ubuntu 24.04 x64; see the repository README for package requirements and AppImage instructions. The AppImage supports extraction when FUSE is unavailable.
