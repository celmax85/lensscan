# Changelog

All notable changes to LensScan are listed here. Versions follow
[Semantic Versioning](https://semver.org/).

## [1.0.0] - 2026-11-22

First stable release: native Linux scanning software for CZUR Lens USB document cameras,
tested with the CZUR Lens 1200 Pro (`1e4f:1227`). No account, cloud service, trial or
telemetry.

### Scanning
- Live preview and capture at the camera's full resolution (3264 × 2448 on the 1200 Pro).
- Page modes: flat page (automatic crop and deskew), curved book (flattening, split at the
  spine, finger removal), several documents at once, area scan, untouched original.
- Color modes: auto optimize, color, grayscale, black and white, white paper and stamps,
  ID / watermark.
- Auto scan when a page is turned, scan sound, rotation, automatic orientation (Tesseract OSD).
- Camera image controls through V4L2; arm height (A4 / A3) used for the stored resolution.

### File manager and batch tools
- Thumbnail list, built-in viewer, insert pages at a given place, rescan one page, import.
- Rotate, flip, color mode, brightness / contrast / sharpness, watermark (text or image),
  trim edges, precise crop, crop overlapping book edges, split facing pages, separate several
  documents, erase handwriting, fill punch holes, blank page detection, rename,
  barcode / QR code reading.
- *Process again*: camera pictures are kept (optional) so a scan can be redone in another mode.

### Output
- PDF, TIFF (multi-page or per page; none, LZW, JPEG or Group 4), PNG, JPEG, BMP, printing.
- Text recognition with Tesseract: searchable PDF, Word (.docx), Excel (.xlsx) and text.
- File names from date and time or from a pattern; JPEG / PNG / BMP with quality presets
  and a stored resolution.

### Presentation mode
- Full screen live view with zoom, freeze, rotation and mirroring.
- Drawing tools: pen, highlighter, line, arrow, rectangle, ellipse, triangle, text; solid,
  dashed and dotted lines; eraser, undo.
- Screenshots, video recording with sound (MP4 / MKV) with pause, screen recording
  (X11, or Wayland through the desktop portal), list of the recorded files.

### Languages
English, French, German, Spanish, Italian, Portuguese (Brazil), Russian, Japanese,
Simplified Chinese.

### Packages
Ubuntu 22.04 / 24.04 / 26.04, Debian 12 / 13, Fedora 43, openSUSE Tumbleweed, AppImage.

[1.0.0]: https://github.com/celmax85/lensscan/releases/tag/v1.0.0
