# Changelog

## [1.0.1] — 2026-11-08

- Add a quiet Fine gears ratchet to reset: 24 teeth per knob turn, a fixed 70% reset-sound level, and a soft seating tick at the end.
- Preserve the original tally click, counting behavior, reset animation, saved data, interface, and “Tally Counter” window title. No sound settings or other controls are added.

## [1.0.0] — 2026-11-07

First release of Tally Counter for Windows 10/11 x64 and Linux x86-64.

- Four mechanical digits display `0000` through `9999`, with animated carries and a fixed upper limit.
- Left-click and Space increment the count. Each physical Space press counts once, including when Reset has keyboard focus.
- The SVG reset knob turns clockwise while the digits rewind in sync. Reset duration grows with the count, up to 2.2 seconds; counting can resume during the animation.
- Successful tallies play a quiet embedded mechanical click. Reset and attempts above the limit are silent.
- Count changes are saved immediately. Normal window geometry and maximized state are restored after a normal close.
- Light and dark appearances follow Windows or GTK settings.
- Native Windows and Linux packages include a Windows ZIP, Linux TAR.GZ, AppImage, and DEB. The source archive contains the build tools, tests, documentation, and the binary checksum manifest.
- First-party code and assets are released under the zlib License.

[1.0.1]: https://github.com/Davoda1/Tally-Counter/releases/tag/v1.0.1
[1.0.0]: https://github.com/Davoda1/Tally-Counter/releases/tag/v1.0.0
