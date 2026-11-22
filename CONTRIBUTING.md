# Contributing to LensScan

Bug reports, translations and pull requests are welcome.

## Pull requests

- Build and run the unit tests before sending a change:
  ```sh
  cmake -S . -B build -G Ninja && cmake --build build
  QT_QPA_PLATFORM=offscreen build/lensscan-tests
  ```
  The build must stay free of compiler warnings (`-Wall -Wextra`).
- Keep the code compatible with Qt 6.2 (Ubuntu 22.04) and C++17.
- Code, comments and user interface strings are in English; every visible string goes
  through `tr()`.
- Tests use synthetic images generated in `tests/tests.cpp`. Do not add photos of real
  documents to the repository.
- Image processing is written from scratch with OpenCV. Do not copy code, models or
  artwork from the vendor's software, and do not add licensing, account or telemetry code.

## Translations

Translations live in `tools/i18n/<lang>.py` (a `TR` dictionary from the English text to
the translation, and `NUMERUS` for plural forms). After changing strings in the code:

```sh
cmake --build build --target update_translations
python3 tools/fill-ts.py
```

`fill-ts.py` prints the strings that still need a translation.

## Other cameras

LensScan has been tested with the CZUR Lens 1200 Pro (`1e4f:1227`). For another model,
open an issue with the output of `lsusb -v -d 1e4f:` and `lensscan-probe`; models are
described in `src/device/DeviceDb.cpp`.

## License

By contributing you agree that your contribution is licensed under the GNU General Public
License version 3 or later.
