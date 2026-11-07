# Contributing

Thanks for helping improve Tally Counter. Small, focused changes are easiest to review.

## Report a bug

Use the [bug report form](https://github.com/Davoda1/Tally-Counter/issues/new?template=bug_report.yml). Include the app version, download type, operating system, exact steps, and what happened. For Linux appearance or placement issues, include the desktop environment and whether the session uses X11 or Wayland. Remove personal details from screenshots and logs.

For security concerns, follow [SECURITY.md](SECURITY.md).

## Suggest a change

Open a [feature request](https://github.com/Davoda1/Tally-Counter/issues/new?template=feature_request.yml) describing the counting task you want to improve. Explain the practical benefit before proposing a particular implementation. Larger interface changes are best discussed before implementation.

## Submit code

1. Fork the repository and make a focused branch.
2. Follow the existing C++17 style and keep platform-specific code in its frontend.
3. Run `make test` for changes to shared logic, then manually exercise affected native behavior.
4. Update the user guide or build instructions if behavior or requirements change.
5. Open a pull request explaining the problem, the change, and how you checked it.

See [Building and packaging](docs/BUILDING.md) for setup. If you change a generated asset, include both its input and generated output.

Preserve the app's basic behavior: one tally per physical press, a limit of 9999, an independent reset control, immediate count saving, and responsive input during animation. Avoid adding a runtime dependency without explaining its purpose and distribution impact.

## License

Contributions to first-party code and assets are made under the project's [zlib License](LICENSE). Include the source and required license notice for any third-party material you add, and mark altered source versions as the license requires.
