# LensScan

[![CI](https://github.com/celmax85/lensscan/actions/workflows/ci.yml/badge.svg)](https://github.com/celmax85/lensscan/actions/workflows/ci.yml)
[![License: GPL v3+](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)

Native Linux scanning software for **CZUR Lens** USB document cameras (tested with the
CZUR Lens 1200 Pro, USB ID `1e4f:1227`). It covers the features of the vendor's Windows
application without an account, cloud service, trial or telemetry.

LensScan is an independent project. It is not affiliated with or endorsed by
CZUR Tech Co., Ltd.; CZUR is a trademark of its owner.

## Features

**Scanning**
- Live preview, capture at the camera's full resolution (3264 × 2448 on the 1200 Pro).
- Page modes: flat page (automatic crop and deskew), curved book (flattening, split at
  the spine, finger removal), several documents at once, area scan, untouched original.
- Color modes: auto optimize, color, grayscale, black and white, white paper and stamps,
  ID / watermark.
- Auto scan when a page is turned, scan sound, rotation, optional automatic orientation
  (Tesseract OSD).
- Camera image controls (brightness, contrast, white balance, anti-flicker, …) through V4L2.

**File manager and batch tools**
- Thumbnail list of the scan folder, built-in viewer (1:1, fit width / height, previous / next).
- Rotate, flip, color mode, brightness / contrast / sharpness, watermark (text or image),
  trim edges, precise crop (per-side margins, mirrored book margins, cut or fill white),
  crop overlapping book edges, split facing pages, separate several documents,
  erase handwriting, fill punch holes, blank page detection, rename, barcode / QR code reading.
- *Process again*: the camera pictures are kept (optional), so a scan can be redone later
  in another page mode or color mode.
- Insert pages at a given place, rescan one page, import images.

**Output**
- PDF, TIFF (multi-page or one file per page; none, LZW, JPEG or Group 4 compression;
  color or gray), PNG, JPEG, BMP, printing with preview.
- Text recognition with Tesseract: searchable PDF, Word (.docx), Excel (.xlsx, tables
  detected on the page) and plain text.
- File names from date and time or from a pattern (`Invoice_#`, 1–9 digits);
  JPEG / PNG / BMP scans with quality presets and a stored resolution (72–1000 dpi,
  or estimated from the camera arm height).

**Presentation mode**
- Full screen live view with zoom, freeze, rotation and mirroring.
- Drawing: pen, highlighter, line, arrow, rectangle, ellipse, triangle, text; solid,
  dashed or dotted lines; eraser and undo.
- Screenshots (PNG, JPEG, BMP), video recording with sound (MP4 or MKV) with pause,
  screen recording (X11, or Wayland through the desktop portal), list of the recorded files.

**Languages**: English, French, German, Spanish, Italian, Portuguese (Brazil), Russian,
Japanese and Simplified Chinese.

## Installation

Download the package for your distribution from the
[Releases](https://github.com/celmax85/lensscan/releases) page. Packages are built for Ubuntu 22.04 / 24.04 / 26.04, Debian 12 / 13, Fedora and openSUSE
Tumbleweed, plus an AppImage for other distributions.

```sh
sudo apt install ./lensscan_*.deb      # Debian, Ubuntu
sudo dnf install ./lensscan-*.rpm      # Fedora
sudo zypper install ./lensscan-*.rpm   # openSUSE
```

After installing, unplug and replug the camera once: the package installs a udev rule
(`70-lensscan.rules`) that gives the logged-in user access to the camera.

With the AppImage, install the rule by hand (the file is
[`packaging/udev/70-lensscan.rules`](packaging/udev/70-lensscan.rules) in this repository):

```sh
sudo cp packaging/udev/70-lensscan.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

### Optional programs

| Feature | Packages (Debian / Ubuntu names) |
| --- | --- |
| Text recognition in more languages | `tesseract-ocr-fra`, `tesseract-ocr-deu`, `tesseract-ocr-jpn`, … |
| Automatic page orientation | `tesseract-ocr-osd` |
| Video and screen recording | `ffmpeg` |
| Screen recording on Wayland | `gstreamer1.0-tools`, `gstreamer1.0-pipewire`, `gstreamer1.0-plugins-good`, and the desktop's `xdg-desktop-portal-*` |

## Troubleshooting

- **No camera found**: check `lsusb | grep 1e4f` and that `/dev/video*` exists; replug the
  camera after installing the package.
- **The preview does not start** (the camera refuses to stream): the USB port often does not
  deliver enough power. Plug the camera directly into another port of the computer, not
  into a hub.
- `lensscan-probe` (installed with the package) lists the camera's formats and controls.

## Building from source

Requirements: CMake ≥ 3.22, a C++17 compiler, Qt ≥ 6.2 (Widgets, Concurrent, PrintSupport,
DBus, LinguistTools), OpenCV 4 (core, imgproc), libjpeg-turbo (TurboJPEG), libtiff,
Tesseract and Leptonica, ZBar.

On Debian / Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
  libopencv-dev libturbojpeg0-dev libtiff-dev libtesseract-dev libleptonica-dev \
  zlib1g-dev libzbar-dev tesseract-ocr-eng
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/lensscan-tests     # unit tests (synthetic images only)
build/lensscan
```

### Building in Docker

Every package is built in containers, so nothing needs to be installed on the host:

```sh
packaging/build-all.sh                 # all targets, output in dist/
packaging/build-all.sh ubuntu-24.04    # one target
packaging/test-install-deb.sh          # install the .deb files in clean containers
packaging/test-appimage.sh
```

Targets: `ubuntu-22.04 ubuntu-24.04 ubuntu-26.04 debian-12 debian-13 fedora-43
opensuse-tumbleweed appimage`.

## Translations

User interface strings are in English in the code (`tr()`). Translations are written in
Python tables, one per language, and copied into the Qt `.ts` files:

```sh
cmake --build build --target update_translations   # refresh i18n/*.ts from the code
python3 tools/fill-ts.py                            # fill them from tools/i18n/<lang>.py
```

`fill-ts.py` lists the strings that have no translation yet. To add a language, create
`tools/i18n/<lang>.py`, add `i18n/lensscan_<lang>.ts` to `qt_add_translations` in
`CMakeLists.txt` and the language to the list in `src/ui/SettingsDialog.cpp`.

## Source layout

```
src/device/   V4L2 capture, UVC extension units, USB descriptors, camera database
src/imaging/  page detection, book flattening, color modes, page tools, JPEG helpers
src/export/   PDF, TIFF and image export
src/ocr/      Tesseract text recognition, DOCX and XLSX writers
src/media/    video recording (ffmpeg), screen sharing portal client
src/ui/       main window, scan window, presentation window, dialogs, icons
src/app/      settings, shortcuts, entry point
tests/        unit tests
tools/        lensscan-probe, development and USB diagnosis scripts, translation tables
packaging/    Debian, RPM and AppImage packaging, udev rule
docker/       build containers
```

## How the camera works

The CZUR Lens 1200 Pro is a standard USB Video Class (UVC) and USB Audio Class device, so the
Linux kernel drives it without extra modules: LensScan reads MJPEG frames through V4L2. The
button on the camera base switches the lamp in the camera itself and is not seen by the
computer. All image processing (page detection, book flattening, color modes) is original
code written with OpenCV.

## Contributing

Bug reports, translations and pull requests are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md).
A tag `vX.Y.Z` builds every package in GitHub Actions and attaches them to a draft release.

## License

Copyright © 2026 LensScan developers.

LensScan is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version. See [LICENSE](LICENSE).

The application icon is original artwork under the same license. Libraries used at run time
keep their own licenses: Qt (LGPL-3.0), OpenCV (Apache-2.0), libjpeg-turbo (BSD-style),
libtiff (libtiff license), Tesseract (Apache-2.0), Leptonica (BSD-2-Clause), ZBar (LGPL-2.1).
