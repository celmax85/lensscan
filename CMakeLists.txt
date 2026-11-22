cmake_minimum_required(VERSION 3.22)
project(lensscan VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(LENSSCAN_BUILD_TESTS "Build unit tests" ON)

include(GNUInstallDirs)
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Concurrent PrintSupport DBus LinguistTools)
find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)
pkg_check_modules(TIFF REQUIRED IMPORTED_TARGET libtiff-4)
pkg_check_modules(TESSERACT REQUIRED IMPORTED_TARGET tesseract)
pkg_check_modules(LEPTONICA REQUIRED IMPORTED_TARGET lept)
pkg_check_modules(ZBAR REQUIRED IMPORTED_TARGET zbar)
find_package(ZLIB REQUIRED)

add_compile_definitions(LENSSCAN_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)
add_compile_options(-Wall -Wextra)

# Core library: everything except the UI, so tests and tools can link it.
add_library(lensscan_core STATIC
  src/device/DeviceDb.cpp
  src/device/SonixAsic.cpp
  src/device/UsbReset.cpp
  src/device/UvcDescriptors.cpp
  src/device/V4l2Device.cpp
  src/device/CameraThread.cpp
  src/imaging/Jpeg.cpp
  src/imaging/PageDetector.cpp
  src/imaging/ColorModes.cpp
  src/imaging/ScanProcessor.cpp
  src/imaging/BookProcessor.cpp
  src/imaging/AutoScan.cpp
  src/imaging/BatchTools.cpp
  src/imaging/DocumentTools.cpp
  src/imaging/Barcode.cpp
  src/media/Recorder.cpp
  src/export/Exporters.cpp
  src/ocr/Ocr.cpp
)
target_include_directories(lensscan_core PUBLIC src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(lensscan_core PUBLIC Qt6::Widgets Qt6::Concurrent ${OpenCV_LIBS}
  PkgConfig::TURBOJPEG PkgConfig::TIFF PkgConfig::TESSERACT PkgConfig::LEPTONICA PkgConfig::ZBAR ZLIB::ZLIB)

qt_add_executable(lensscan
  src/app/main.cpp
  src/app/AppSettings.cpp
  src/ui/Icons.cpp
  src/ui/PreviewWidget.cpp
  src/ui/ScanWindow.cpp
  src/ui/MainWindow.cpp
  src/ui/SettingsDialog.cpp
  src/ui/BatchDialogs.cpp
  src/ui/ImageViewer.cpp
  src/ui/ToolDialogs.cpp
  src/media/ScreenCastPortal.cpp
  src/app/Shortcuts.cpp
  src/ui/PresentationWindow.cpp
  resources/lensscan.qrc
)
target_link_libraries(lensscan PRIVATE lensscan_core Qt6::PrintSupport Qt6::DBus)

qt_add_translations(lensscan TS_FILES i18n/lensscan_fr.ts i18n/lensscan_en.ts i18n/lensscan_de.ts i18n/lensscan_es.ts i18n/lensscan_it.ts i18n/lensscan_pt.ts i18n/lensscan_ru.ts i18n/lensscan_ja.ts i18n/lensscan_zh_CN.ts
  RESOURCE_PREFIX /i18n)

add_executable(lensscan-probe tools/lensscan-probe.cpp)
target_link_libraries(lensscan-probe PRIVATE lensscan_core)

install(TARGETS lensscan lensscan-probe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/io.github.celmax85.LensScan.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/io.github.celmax85.LensScan.metainfo.xml
  DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES resources/icons/app/lensscan.svg
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
  RENAME io.github.celmax85.LensScan.svg)
install(FILES packaging/udev/70-lensscan.rules
  DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)

if(LENSSCAN_BUILD_TESTS)
  enable_testing()
  add_executable(lensscan-tests tests/tests.cpp)
  target_link_libraries(lensscan-tests PRIVATE lensscan_core)
  add_test(NAME lensscan-tests COMMAND lensscan-tests)
endif()
