# Security

Tally Counter is a local desktop application. It reads and writes a small count and window-settings file in the current user's application-data directory and plays an embedded sound. It has no account, telemetry, updater, or network feature.

## Report a vulnerability

Please report suspected security issues against the latest release or current default branch. Include the affected version, operating system, package type, reproduction steps, and likely impact.

If this repository's **Security → Report a vulnerability** option is available, use it for a private report. If that option is unavailable, open an issue asking for a private reporting channel without including exploit details or sensitive data. Avoid posting credentials, personal files, or a working exploit in a public issue.

Ordinary crashes, appearance problems, and counting bugs can use the [bug report form](https://github.com/Davoda1/Tally-Counter/issues/new?template=bug_report.yml).

## Dependencies

The Linux TAR.GZ and DEB use system GUI and audio libraries. Keep those packages updated through your distribution. The AppImage bundles GUI libraries and is updated by replacing the AppImage with a new release. See [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) for distributed component notices.
