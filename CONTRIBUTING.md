# Contributing

Thanks for looking. ClawTicker is a small, focused project: one board (DIYMORE ES3C28P), one daemon, a handful of screens. Bug reports, fixes and documentation improvements are very welcome. Please open an issue before starting a large feature or support for another board.

## Before you send a pull request

1. Daemon tests pass: `python3 -I -m unittest discover -s daemon/tests`
2. The firmware builds: `cd firmware && pio run`
3. If you changed the UI, attach a screenshot taken with `tools/shot.py`
4. If you changed the daemon payload, update [docs/payload.md](docs/payload.md)
5. Keep the daemon dependency-free (Python standard library only) and the firmware within the existing libraries

## Reporting a bug

Include: board photo or marking, what the screen shows, the output of the serial command `status`, your OS and Python version, and the daemon log. Do **not** post tokens, Wi-Fi passwords or the contents of your Claude or Codex session logs.

## Licensing

By contributing you agree that your contribution is released under the MIT license of this repository. Do not add third-party artwork, fonts or code unless its license allows redistribution, and list it in `NOTICE.md`.
