Name:           lensscan
Version:        1.0.0
Release:        1%{?dist}
Summary:        Document scanning for CZUR Lens USB document cameras
License:        GPL-3.0-or-later
URL:            https://github.com/celmax85/lensscan
Source0:        lensscan-%{version}.tar.gz

BuildRequires:  gcc-c++ cmake make pkgconfig
BuildRequires:  cmake(Qt6Widgets) cmake(Qt6Concurrent) cmake(Qt6PrintSupport) cmake(Qt6DBus) cmake(Qt6LinguistTools)
BuildRequires:  pkgconfig(opencv4) pkgconfig(libturbojpeg) pkgconfig(libtiff-4)
BuildRequires:  pkgconfig(tesseract) pkgconfig(lept) pkgconfig(zlib) pkgconfig(zbar)
BuildRequires:  systemd-rpm-macros
%if 0%{?suse_version}
BuildRequires:  dejavu-fonts
Requires:       tesseract-ocr-traineddata-eng
Recommends:     tesseract-ocr-traineddata-fra tesseract-ocr-traineddata-osd
Recommends:     gstreamer-plugin-pipewire gstreamer-plugins-good gstreamer-utils
%else
Requires:       tesseract-langpack-eng
Recommends:     tesseract-langpack-fra tesseract-osd
Recommends:     pipewire-gstreamer gstreamer1-plugins-good gstreamer1
%endif

Recommends:     /usr/bin/ffmpeg

%description
LensScan drives CZUR Lens document cameras (USB 1e4f:1227 and other UVC models)
on Linux: live preview, automatic page detection and deskewing, curved book
flattening, colour modes, batch page tools, export to PDF, TIFF, PNG, JPEG and
BMP, text recognition (Tesseract) to searchable PDF, Word, Excel and text files,
and a presentation mode with drawing, video and screen recording.
This is an independent project, not affiliated with CZUR.

%prep
%autosetup

%build
%cmake -DCMAKE_BUILD_TYPE=Release -DLENSSCAN_BUILD_TESTS=ON
%cmake_build

%install
%cmake_install

%check
QT_QPA_PLATFORM=offscreen %ctest

%post
%udev_rules_update
udevadm trigger --action=change --subsystem-match=video4linux --subsystem-match=input --subsystem-match=usb >/dev/null 2>&1 || :

%postun
%udev_rules_update

%files
%license LICENSE
%{_bindir}/lensscan
%{_bindir}/lensscan-probe
%{_udevrulesdir}/70-lensscan.rules
%{_datadir}/applications/io.github.celmax85.LensScan.desktop
%{_datadir}/metainfo/io.github.celmax85.LensScan.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/io.github.celmax85.LensScan.svg

%changelog
* Sun Nov 22 2026 LensScan developers <62764007+celmax85@users.noreply.github.com> - 1.0.0-1
- First stable release, see CHANGELOG.md

* Sun Nov 22 2026 LensScan developers <62764007+celmax85@users.noreply.github.com> - 0.1.0-1
- Initial release
