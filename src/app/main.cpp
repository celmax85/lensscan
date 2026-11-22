// SPDX-License-Identifier: GPL-3.0-or-later
// LensScan: scanning software for CZUR Lens document cameras on Linux.
#include "app/AppSettings.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QLocale>
#include <QDir>
#include <QTimer>
#include <cstdio>
#include <cstring>

#include <QStyle>
#include <QStyleHints>
#include <QStyleFactory>
#include <QTranslator>

#include <cstdio>

namespace {

const char *kStyle = R"(
QWidget { color: #2c3140; }
QDialog, QMessageBox, #filePane, #scanWindow { background: #f7f7f8; }
QLineEdit, QSpinBox, QComboBox, QListWidget { background: #ffffff; color: #2c3140; }
QComboBox QAbstractItemView { background: #ffffff; color: #2c3140; selection-background-color: #d6e6ff; }
QMenu { background: #ffffff; color: #2c3140; }
QMenu::item:selected { background: #d6e6ff; }
QToolTip { background: #282e41; color: #ffffff; border: none; padding: 4px; }
QRadioButton:disabled, QCheckBox:disabled, QLabel:disabled { color: #a0a4ad; }
#topBar { background: #282e41; }
#topBar QLabel#topLabel, #topBar QLabel#appTitle { color: #e8eaf0; }
#topBar QLabel#appTitle { font-size: 15px; font-weight: 600; padding-left: 4px; }
#topBar QToolButton { color: #ffffff; border: none; padding: 6px 8px; border-radius: 4px; }
#topBar QToolButton:hover { background: #3b4260; }
#topBar QToolButton:pressed { background: #1d2231; }

#sidePanel, #toolPane { background: #f0f0f0; }
#toolPane { border-left: 1px solid #d8d8d8; }
#sidePanel { border-right: 1px solid #d8d8d8; }
QLabel#sectionLabel { color: #6b7080; font-weight: 600; padding: 6px 0 4px 0; }

QPushButton#sideButton { text-align: left; padding: 5px 10px; border: none; border-radius: 4px;
                         background: transparent; color: #2c3140; }
QPushButton#sideButton:hover { background: #e1e5ef; }
QPushButton#sideButton:pressed { background: #cfd6e6; }
QPushButton#sideButton:disabled { color: #a0a0a0; }

QPushButton#bigScanButton { background: #3d8bfd; color: white; font-size: 15px; font-weight: 600;
                            padding: 11px; border: none; border-radius: 6px; }
QPushButton#bigScanButton:hover { background: #5a9cff; }
QPushButton#bigScanButton:pressed { background: #2a74e0; }

#cameraPane { background: #eaebed; border-top: 1px solid #d8d8d8; }
QPushButton#scanButton { background: #3d8bfd; border: 4px solid #ffffff; border-radius: 38px; }
QPushButton#scanButton:hover { background: #5a9cff; }
QPushButton#scanButton:pressed { background: #2a74e0; }
QPushButton#scanButton:disabled { background: #a9b6cc; }
QToolButton#roundButton { background: #ffffff; border: 1px solid #d0d3da; border-radius: 24px; }
QToolButton#roundButton:hover { background: #e1e5ef; }

#pageList { background: #d8d8d8; border: none; border-left: 1px solid #c8c8c8; }
#fileList { background: #ffffff; border: 1px solid #d8d8d8; }
#fileList::item:selected { background: #d6e6ff; color: #1d2231; border-radius: 4px; }
#statusBar { background: #f0f0f0; border-top: 1px solid #d8d8d8; color: #555b6b; }
QLabel#folderLabel { color: #555b6b; }
)";

} // namespace

int main(int argc, char **argv)
{
    // Answer --version without a display (scripts, package tests, ssh).
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::printf("LensScan %s\n", LENSSCAN_VERSION);
            return 0;
        }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LensScan"));
    QApplication::setOrganizationName(QStringLiteral("LensScan"));
    QApplication::setApplicationVersion(QStringLiteral(LENSSCAN_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("io.github.celmax85.LensScan"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Scanning software for CZUR Lens document cameras"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption scanOpt(QStringLiteral("scan"), QStringLiteral("Open the scan window directly."));
    parser.addOption(scanOpt);
    QCommandLineOption presentOpt(QStringLiteral("present"), QStringLiteral("Open the presentation window directly."));
    parser.addOption(presentOpt);
    parser.process(app);

    lensscan::AppSettings settings = lensscan::AppSettings::load();

    const QLocale locale = settings.language.isEmpty() ? QLocale::system() : QLocale(settings.language);
    QLocale::setDefault(locale);
    QTranslator qtTr, appTr;
    if (qtTr.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTr);
    if (appTr.load(locale, QStringLiteral("lensscan"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&appTr);

    // The layout is a light design (like the original software). Use Fusion with its
    // light palette so a dark desktop theme cannot make the text unreadable.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        QApplication::setStyle(fusion);
        QApplication::setPalette(fusion->standardPalette());
    }
    app.setStyleSheet(QString::fromLatin1(kStyle));

    lensscan::MainWindow w(settings);
    if (parser.isSet(scanOpt))
        w.openScanWindow();
    else if (parser.isSet(presentOpt))
        w.openPresentation();
    else
        w.show();
    // Test hook: LENSSCAN_SNAPSHOT=dir[:seconds[:scan|call=<slot>]] saves every visible
    // window as PNG after the delay, then quits. "scan" triggers one scan first;
    // "call=showSettings" calls that slot of the windows that have it (opens a dialog).
    const QStringList snap = qEnvironmentVariable("LENSSCAN_SNAPSHOT").split(QLatin1Char(':'));
    if (!snap.value(0).isEmpty()) {
        const int delay = snap.value(1, QStringLiteral("8")).toInt() * 1000;
        if (snap.value(2).startsWith(QLatin1String("call=")))
            QTimer::singleShot(delay / 2, &app, [slot = snap.value(2).mid(5).toLatin1()] {
                for (QWidget *t : QApplication::topLevelWidgets())
                    if (t->isVisible() && t->metaObject()->indexOfSlot(slot + "()") >= 0)
                        QMetaObject::invokeMethod(t, slot.constData(), Qt::QueuedConnection);
            });
        else if (snap.value(2) == QLatin1String("scan"))
            QTimer::singleShot(delay / 2, &app, [] {
                for (QWidget *t : QApplication::topLevelWidgets())
                    if (t->isVisible() && t->metaObject()->indexOfSlot("scan()") >= 0)
                        QMetaObject::invokeMethod(t, "scan");
            });
        QTimer::singleShot(delay, &app, [dir = snap.value(0)] {
            int i = 0;
            for (QWidget *t : QApplication::topLevelWidgets())
                if (t->isVisible())
                    t->grab().save(QDir(dir).filePath(QStringLiteral("window%1-%2.png").arg(++i).arg(
                        QString::fromLatin1(t->metaObject()->className()).section(QLatin1Char(':'), -1))));
            QApplication::quit();
        });
    }
    return app.exec();
}
