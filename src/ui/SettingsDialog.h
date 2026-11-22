// SPDX-License-Identifier: GPL-3.0-or-later
// Settings: scan folder, file naming, quality, language, camera image controls
// (applied live), and PDF output options.
#pragma once

#include "app/AppSettings.h"
#include "device/V4l2Device.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QKeySequenceEdit;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

namespace lensscan {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(AppSettings &settings, const QVector<ControlInfo> &controls, QWidget *parent = nullptr);

Q_SIGNALS:
    void controlChanged(uint32_t id, const QString &name, int32_t value);

protected:
    void accept() override;

private:
    QWidget *buildGeneral();
    QWidget *buildCamera(const QVector<ControlInfo> &controls);
    QWidget *buildDocuments();
    QWidget *buildShortcuts();
    QWidget *buildPresentation();
    void updateNamePreview();

    AppSettings &m_settings;
    QLineEdit *m_dir = nullptr;
    QLineEdit *m_prefix = nullptr;
    QComboBox *m_quality = nullptr;
    QComboBox *m_naming = nullptr, *m_format = nullptr, *m_dpi = nullptr, *m_height = nullptr;
    QLineEdit *m_pattern = nullptr;
    QSpinBox *m_digits = nullptr, *m_dpiOther = nullptr;
    QLabel *m_namePreview = nullptr;
    QCheckBox *m_keepOriginals = nullptr;
    struct OutputRow {
        QLineEdit *dir;
        QComboBox *format;
    };
    OutputRow m_snapshots{}, m_videos{}, m_screen{};
    QComboBox *m_language = nullptr;
    QComboBox *m_pdfPage = nullptr;
    QSpinBox *m_pdfDpi = nullptr;
    QListWidget *m_ocrLangs = nullptr;
    QCheckBox *m_orderBeforeExport = nullptr;
    QHash<QString, QKeySequenceEdit *> m_keyEdits;
};

} // namespace lensscan
