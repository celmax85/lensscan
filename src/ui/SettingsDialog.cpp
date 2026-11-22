// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsDialog.h"

#include "app/Shortcuts.h"

#include "ocr/Ocr.h"

#include <QListWidget>
#include <algorithm>

#include <QCheckBox>
#include <QLocale>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QKeySequenceEdit>
#include <QScrollArea>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <linux/videodev2.h>

namespace lensscan {

SettingsDialog::SettingsDialog(AppSettings &settings, const QVector<ControlInfo> &controls, QWidget *parent)
    : QDialog(parent), m_settings(settings)
{
    setWindowTitle(tr("Settings"));
    resize(640, 600);
    auto *l = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildGeneral(), tr("General"));
    tabs->addTab(buildCamera(controls), tr("Camera"));
    tabs->addTab(buildDocuments(), tr("Documents"));
    tabs->addTab(buildPresentation(), tr("Presentation"));
    tabs->addTab(buildShortcuts(), tr("Shortcuts"));
    l->addWidget(tabs);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    l->addWidget(buttons);
}

QWidget *SettingsDialog::buildGeneral()
{
    auto *w = new QWidget(this);
    auto *f = new QFormLayout(w);

    auto *dirRow = new QHBoxLayout;
    m_dir = new QLineEdit(m_settings.outputDir, w);
    auto *browse = new QPushButton(tr("Browse..."), w);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Scan folder"), m_dir->text());
        if (!d.isEmpty())
            m_dir->setText(d);
    });
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(browse);
    f->addRow(tr("Scan folder:"), dirRow);

    // File names: prefix + date and time, or a pattern with a serial number.
    m_naming = new QComboBox(w);
    m_naming->addItem(tr("Prefix, date and time"));
    m_naming->addItem(tr("Pattern with a number"));
    m_naming->setCurrentIndex(m_settings.namePattern.isEmpty() ? 0 : 1);
    f->addRow(tr("File names:"), m_naming);
    m_prefix = new QLineEdit(m_settings.filePrefix, w);
    m_prefix->setToolTip(tr("Files are named <prefix>_<date>_<time>_<number>"));
    f->addRow(tr("File name prefix:"), m_prefix);
    auto *patternRow = new QHBoxLayout;
    m_pattern = new QLineEdit(m_settings.namePattern.isEmpty() ? QStringLiteral("image#") : m_settings.namePattern, w);
    m_pattern->setMaxLength(40);
    m_pattern->setToolTip(tr("\"#\" marks the place of the number, for example Invoice_# or Book-#-scan. "
                             "At most 40 characters."));
    m_digits = new QSpinBox(w);
    m_digits->setRange(1, 9);
    m_digits->setValue(m_settings.nameDigits);
    m_digits->setToolTip(tr("Digits of the number (at most 9), with leading zeros"));
    patternRow->addWidget(m_pattern, 1);
    patternRow->addWidget(new QLabel(tr("Digits:"), w));
    patternRow->addWidget(m_digits);
    f->addRow(tr("Name pattern:"), patternRow);
    m_namePreview = new QLabel(w);
    m_namePreview->setStyleSheet(QStringLiteral("color:#666;"));
    f->addRow(tr("Example:"), m_namePreview);
    const auto namingChanged = [this] {
        const bool pattern = m_naming->currentIndex() == 1;
        m_prefix->setEnabled(!pattern);
        m_pattern->setEnabled(pattern);
        m_digits->setEnabled(pattern);
        updateNamePreview();
    };
    connect(m_naming, qOverload<int>(&QComboBox::currentIndexChanged), this, namingChanged);
    connect(m_prefix, &QLineEdit::textChanged, this, &SettingsDialog::updateNamePreview);
    connect(m_pattern, &QLineEdit::textChanged, this, &SettingsDialog::updateNamePreview);
    connect(m_digits, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsDialog::updateNamePreview);

    m_format = new QComboBox(w);
    m_format->addItem(tr("JPEG (small files)"), QStringLiteral("jpg"));
    m_format->addItem(tr("PNG (lossless)"), QStringLiteral("png"));
    m_format->addItem(tr("BMP (uncompressed)"), QStringLiteral("bmp"));
    m_format->setCurrentIndex(std::max(0, m_format->findData(m_settings.imageFormat)));
    m_format->setToolTip(tr("Black and white scans are always saved as PNG, which is smaller than JPEG for them"));
    f->addRow(tr("Image format:"), m_format);

    m_quality = new QComboBox(w);
    m_quality->addItem(tr("Best (largest files)"), 92);
    m_quality->addItem(tr("Medium"), 85);
    m_quality->addItem(tr("Low (smallest files)"), 70);
    if (m_quality->findData(m_settings.jpegQuality) < 0)
        m_quality->addItem(tr("Custom (%1)").arg(m_settings.jpegQuality), m_settings.jpegQuality);
    m_quality->setCurrentIndex(m_quality->findData(m_settings.jpegQuality));
    m_quality->setToolTip(tr("JPEG image quality"));
    f->addRow(tr("Image quality:"), m_quality);
    connect(m_format, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, namingChanged] {
        m_quality->setEnabled(m_format->currentData().toString() == QLatin1String("jpg"));
        namingChanged();
    });
    m_quality->setEnabled(m_format->currentData().toString() == QLatin1String("jpg"));

    auto *dpiRow = new QHBoxLayout;
    m_dpi = new QComboBox(w);
    m_dpi->addItem(tr("Automatic (from the camera height)"), 0);
    for (int d : {96, 150, 200, 250, 300, 400, 600})
        m_dpi->addItem(d == 300 ? tr("%1 dpi (default)").arg(d) : tr("%1 dpi").arg(d), d);
    m_dpi->addItem(tr("Other"), -1);
    m_dpiOther = new QSpinBox(w);
    m_dpiOther->setRange(72, 1000);
    m_dpiOther->setSuffix(tr(" dpi"));
    m_dpiOther->setValue(m_settings.dpi > 0 ? m_settings.dpi : 300);
    const int dpiIndex = m_dpi->findData(m_settings.dpi);
    m_dpi->setCurrentIndex(dpiIndex >= 0 ? dpiIndex : m_dpi->count() - 1);
    m_dpiOther->setVisible(m_dpi->currentData().toInt() < 0);
    connect(m_dpi, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { m_dpiOther->setVisible(m_dpi->currentData().toInt() < 0); });
    m_dpi->setToolTip(tr("Resolution written into the image files; it sets the printed size of the pages "
                         "(72 to 1000 dpi)"));
    dpiRow->addWidget(m_dpi, 1);
    dpiRow->addWidget(m_dpiOther);
    f->addRow(tr("Resolution:"), dpiRow);

    m_keepOriginals = new QCheckBox(tr("Keep the camera pictures, to process scans again later"), w);
    m_keepOriginals->setChecked(m_settings.keepOriginals);
    m_keepOriginals->setToolTip(tr("Needed by More tools > Process again to go back to the uncropped picture or "
                                   "to another page mode. Kept in the hidden .lensscan folder of the scan folder."));
    auto *keepRow = new QHBoxLayout;
    keepRow->addWidget(m_keepOriginals, 1);
    auto *purge = new QPushButton(tr("Delete kept pictures"), w);
    connect(purge, &QPushButton::clicked, this, [this] {
        QDir dir(originalsDir(m_settings.outputDir));
        const QFileInfoList all = dir.entryInfoList(QDir::Files);
        qint64 bytes = 0;
        for (const QFileInfo &fi : all)
            bytes += fi.size();
        if (all.isEmpty()) {
            QMessageBox::information(this, tr("Camera pictures"), tr("No camera picture is kept for this scan folder."));
            return;
        }
        if (QMessageBox::question(this, tr("Camera pictures"),
                                  tr("Delete the %n kept camera picture(s) (%1)? The scans stay, but can no longer "
                                     "be processed again from their camera picture.",
                                     nullptr, int(all.size()))
                                      .arg(QLocale().formattedDataSize(bytes))) != QMessageBox::Yes)
            return;
        for (const QFileInfo &fi : all)
            QFile::remove(fi.absoluteFilePath());
    });
    keepRow->addWidget(purge);
    f->addRow(QString(), keepRow);
    namingChanged();

    m_language = new QComboBox(w);
    m_language->addItem(tr("System default"), QString());
    m_language->addItem(QStringLiteral("Deutsch"), QStringLiteral("de"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_language->addItem(QStringLiteral("Español"), QStringLiteral("es"));
    m_language->addItem(QStringLiteral("Français"), QStringLiteral("fr"));
    m_language->addItem(QStringLiteral("Italiano"), QStringLiteral("it"));
    m_language->addItem(QStringLiteral("Português"), QStringLiteral("pt"));
    m_language->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    m_language->addItem(QStringLiteral("日本語"), QStringLiteral("ja"));
    m_language->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    m_language->setCurrentIndex(qMax(0, m_language->findData(m_settings.language)));
    f->addRow(tr("Language:"), m_language);
    return w;
}

QWidget *SettingsDialog::buildCamera(const QVector<ControlInfo> &controls)
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *w = new QWidget(scroll);
    auto *f = new QFormLayout(w);
    m_height = new QComboBox(w);
    m_height->addItem(tr("A4 height (arm low)"), false);
    m_height->addItem(tr("A3 height (arm high)"), true);
    m_height->setCurrentIndex(m_settings.highCamera ? 1 : 0);
    m_height->setToolTip(tr("Position of the camera arm. Used for the automatic resolution of the scans "
                            "(Settings > General > Resolution)."));
    f->addRow(tr("Camera height:"), m_height);
    if (controls.isEmpty()) {
        f->addRow(new QLabel(tr("Open the scan window with the camera connected to adjust its image settings."), w));
        scroll->setWidget(w);
        return scroll;
    }
    QVector<QPair<QWidget *, ControlInfo>> resettable;
    for (const ControlInfo &c : controls) {
        const QString name = QString::fromStdString(c.name);
        const uint32_t id = c.id;
        if (c.type == V4L2_CTRL_TYPE_BOOLEAN) {
            auto *cb = new QCheckBox(w);
            cb->setChecked(c.value);
            connect(cb, &QCheckBox::toggled, this, [this, id, name](bool on) { Q_EMIT controlChanged(id, name, on); });
            f->addRow(name + QLatin1Char(':'), cb);
            resettable.append({cb, c});
        } else if (c.type == V4L2_CTRL_TYPE_MENU || c.type == V4L2_CTRL_TYPE_INTEGER_MENU) {
            auto *combo = new QComboBox(w);
            for (const auto &m : c.menu)
                combo->addItem(QString::fromStdString(m.second), m.first);
            combo->setCurrentIndex(qMax(0, combo->findData(c.value)));
            connect(combo, qOverload<int>(&QComboBox::activated), this, [this, combo, id, name](int i) {
                Q_EMIT controlChanged(id, name, combo->itemData(i).toInt());
            });
            f->addRow(name + QLatin1Char(':'), combo);
            resettable.append({combo, c});
        } else if (c.type == V4L2_CTRL_TYPE_INTEGER) {
            auto *row = new QHBoxLayout;
            auto *slider = new QSlider(Qt::Horizontal, w);
            slider->setRange(c.minimum, c.maximum);
            slider->setSingleStep(qMax(1, c.step));
            slider->setValue(c.value);
            auto *val = new QLabel(QString::number(c.value), w);
            val->setMinimumWidth(40);
            connect(slider, &QSlider::valueChanged, this, [this, val, id, name](int v) {
                val->setText(QString::number(v));
                Q_EMIT controlChanged(id, name, v);
            });
            row->addWidget(slider, 1);
            row->addWidget(val);
            f->addRow(name + QLatin1Char(':'), row);
            resettable.append({slider, c});
        }
    }
    auto *reset = new QPushButton(tr("Restore defaults"), w);
    connect(reset, &QPushButton::clicked, this, [this, resettable] {
        for (const auto &r : resettable) {
            if (auto *cb = qobject_cast<QCheckBox *>(r.first))
                cb->setChecked(r.second.defaultValue);
            else if (auto *s = qobject_cast<QSlider *>(r.first))
                s->setValue(r.second.defaultValue);
            else if (auto *combo = qobject_cast<QComboBox *>(r.first)) {
                combo->setCurrentIndex(qMax(0, combo->findData(r.second.defaultValue)));
                Q_EMIT controlChanged(r.second.id, QString::fromStdString(r.second.name), r.second.defaultValue);
            }
        }
    });
    f->addRow(QString(), reset);
    scroll->setWidget(w);
    return scroll;
}

QWidget *SettingsDialog::buildDocuments()
{
    auto *w = new QWidget(this);
    auto *f = new QFormLayout(w);
    m_pdfPage = new QComboBox(w);
    m_pdfPage->addItem(tr("Same as the image"), 0);
    m_pdfPage->addItem(tr("A4"), 1);
    m_pdfPage->setCurrentIndex(m_settings.pdfPageSize == PdfOptions::PageSize::A4 ? 1 : 0);
    f->addRow(tr("PDF page size:"), m_pdfPage);
    m_pdfDpi = new QSpinBox(w);
    m_pdfDpi->setRange(72, 1200);
    m_pdfDpi->setSuffix(tr(" dpi"));
    m_pdfDpi->setValue(m_settings.pdfDpi);
    m_pdfDpi->setToolTip(tr("With 'Same as the image', sets the printed size: pixels / dpi"));
    f->addRow(tr("PDF resolution:"), m_pdfDpi);
    m_orderBeforeExport = new QCheckBox(tr("Choose the page order before creating a document"), w);
    m_orderBeforeExport->setToolTip(tr("Shows the pages before a PDF, TIFF or OCR export so they can be reordered"));
    m_orderBeforeExport->setChecked(m_settings.orderBeforeExport);
    f->addRow(QString(), m_orderBeforeExport);

    // OCR languages: the installed Tesseract data, ticked = used together.
    m_ocrLangs = new QListWidget(w);
    m_ocrLangs->setToolTip(tr("Languages present in the documents. Fewer languages give faster, more accurate "
                              "results."));
    const QStringList avail = ocrAvailableLanguages();
    const QStringList chosen = (m_settings.ocrLanguages.isEmpty() ? ocrDefaultLanguages() : m_settings.ocrLanguages)
                                   .split(QLatin1Char('+'), Qt::SkipEmptyParts);
    QList<QPair<QString, QString>> named;
    for (const QString &code : avail)
        named.append({ocrLanguageName(code), code});
    std::sort(named.begin(), named.end());
    for (const auto &n : named) {
        auto *item = new QListWidgetItem(n.first, m_ocrLangs);
        item->setData(Qt::UserRole, n.second);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(chosen.contains(n.second) ? Qt::Checked : Qt::Unchecked);
    }
    m_ocrLangs->setMinimumHeight(140);
    if (avail.isEmpty()) {
        auto *none = new QLabel(tr("No Tesseract language data installed. Install for example the "
                                   "tesseract-ocr-fra package."),
                                w);
        none->setWordWrap(true);
        f->addRow(tr("OCR languages:"), none);
        m_ocrLangs->hide();
    } else {
        f->addRow(tr("OCR languages:"), m_ocrLangs);
    }
    return w;
}

QWidget *SettingsDialog::buildShortcuts()
{
    auto *w = new QWidget(this);
    auto *outer = new QVBoxLayout(w);
    auto *scroll = new QScrollArea(w);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget(scroll);
    auto *f = new QFormLayout(inner);
    QString group;
    for (const ShortcutDef &d : Shortcuts::instance().definitions()) {
        if (d.group != group) {
            group = d.group;
            auto *g = new QLabel(QStringLiteral("<b>%1</b>").arg(group.toHtmlEscaped()), inner);
            f->addRow(g);
        }
        auto *edit = new QKeySequenceEdit(Shortcuts::instance().key(d.id), inner);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        edit->setClearButtonEnabled(true);
        edit->setMaximumSequenceLength(1);
#endif
        edit->setProperty("defaultKey", d.defaultKey.toString(QKeySequence::PortableText));
        m_keyEdits.insert(d.id, edit);
        f->addRow(d.text, edit);
    }
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);
    auto *row = new QHBoxLayout;
    auto *defaults = new QPushButton(tr("Restore defaults"), w);
    connect(defaults, &QPushButton::clicked, this, [this] {
        for (QKeySequenceEdit *e : std::as_const(m_keyEdits))
            e->setKeySequence(QKeySequence(e->property("defaultKey").toString(), QKeySequence::PortableText));
    });
    auto *clear = new QPushButton(tr("Clear all"), w);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Shortcuts"), tr("Remove all keyboard shortcuts?")) == QMessageBox::Yes)
            for (QKeySequenceEdit *e : std::as_const(m_keyEdits))
                e->clear();
    });
    row->addWidget(defaults);
    row->addWidget(clear);
    row->addStretch(1);
    outer->addLayout(row);
    return w;
}

void SettingsDialog::accept()
{
    QHash<QString, QKeySequence> keys;
    for (auto it = m_keyEdits.cbegin(); it != m_keyEdits.cend(); ++it)
        keys.insert(it.key(), it.value()->keySequence());
    const QStringList clash = Shortcuts::conflicts(keys);
    if (!clash.isEmpty()) {
        QMessageBox::warning(this, tr("Shortcuts"),
                             tr("These actions use the same key:\n%1\n\nChoose another key for one of them.")
                                 .arg(clash.join(QLatin1Char('\n'))));
        return;
    }
    Shortcuts::instance().setKeys(keys);
    const QString lang = m_language->currentData().toString();
    if (lang != m_settings.language)
        QMessageBox::information(this, tr("Language"), tr("The new language will be used after restarting LensScan."));
    m_settings.outputDir = m_dir->text().trimmed().isEmpty() ? AppSettings::defaultOutputDir() : m_dir->text().trimmed();
    m_settings.filePrefix = m_prefix->text().trimmed().isEmpty() ? QStringLiteral("Scan") : m_prefix->text().trimmed();
    m_settings.jpegQuality = m_quality->currentData().toInt();
    m_settings.namePattern = m_naming->currentIndex() == 1 ? m_pattern->text().trimmed().left(40) : QString();
    if (m_naming->currentIndex() == 1 && m_settings.namePattern.isEmpty())
        m_settings.namePattern = QStringLiteral("image#");
    m_settings.nameDigits = m_digits->value();
    m_settings.imageFormat = m_format->currentData().toString();
    m_settings.dpi = m_dpi->currentData().toInt() < 0 ? m_dpiOther->value() : m_dpi->currentData().toInt();
    m_settings.highCamera = m_height->currentData().toBool();
    m_settings.keepOriginals = m_keepOriginals->isChecked();
    const auto folder = [](const OutputRow &r) {
        const QString d = r.dir->text().trimmed();
        return d.isEmpty() ? QString() : d;
    };
    m_settings.snapshotDir = folder(m_snapshots);
    m_settings.videoDir = folder(m_videos);
    m_settings.screenDir = folder(m_screen);
    m_settings.snapshotFormat = m_snapshots.format->currentData().toString();
    m_settings.videoFormat = m_videos.format->currentData().toString();
    m_settings.screenFormat = m_screen.format->currentData().toString();
    m_settings.language = lang;
    m_settings.orderBeforeExport = m_orderBeforeExport->isChecked();
    m_settings.pdfPageSize = m_pdfPage->currentData().toInt() ? PdfOptions::PageSize::A4 : PdfOptions::PageSize::FromImage;
    m_settings.pdfDpi = m_pdfDpi->value();
    QStringList langs;
    for (int i = 0; i < m_ocrLangs->count(); ++i)
        if (m_ocrLangs->item(i)->checkState() == Qt::Checked)
            langs << m_ocrLangs->item(i)->data(Qt::UserRole).toString();
    m_settings.ocrLanguages = langs.join(QLatin1Char('+'));
    m_settings.save();
    QDialog::accept();
}

void SettingsDialog::updateNamePreview()
{
    const QString suffix = scanSuffix(m_format->currentData().toString());
    QString name;
    if (m_naming->currentIndex() == 1) {
        QString pattern = m_pattern->text().trimmed();
        if (pattern.isEmpty())
            pattern = QStringLiteral("image#");
        name = patternName(pattern, m_digits->value(), 1);
    } else {
        const QString prefix = m_prefix->text().trimmed().isEmpty() ? QStringLiteral("Scan") : m_prefix->text().trimmed();
        name = QStringLiteral("%1_%2_001").arg(prefix, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    }
    m_namePreview->setText(name + QLatin1Char('.') + suffix);
}

QWidget *SettingsDialog::buildPresentation()
{
    auto *w = new QWidget(this);
    auto *f = new QFormLayout(w);
    const auto row = [this, w, f](const QString &label, const QString &dir, const QString &format,
                                  const QList<QPair<QString, QString>> &formats, OutputRow *out) {
        auto *h = new QHBoxLayout;
        out->dir = new QLineEdit(dir, w);
        out->dir->setPlaceholderText(tr("Scan folder"));
        out->dir->setClearButtonEnabled(true);
        auto *browse = new QPushButton(tr("Browse..."), w);
        QLineEdit *edit = out->dir;
        connect(browse, &QPushButton::clicked, this, [this, edit] {
            const QString start = edit->text().isEmpty() ? m_settings.outputDir : edit->text();
            const QString d = QFileDialog::getExistingDirectory(this, tr("Folder"), start);
            if (!d.isEmpty())
                edit->setText(d);
        });
        out->format = new QComboBox(w);
        for (const auto &fmt : formats)
            out->format->addItem(fmt.second, fmt.first);
        out->format->setCurrentIndex(std::max(0, out->format->findData(format)));
        h->addWidget(out->dir, 1);
        h->addWidget(browse);
        h->addWidget(out->format);
        f->addRow(label, h);
    };
    const QList<QPair<QString, QString>> images = {{QStringLiteral("png"), QStringLiteral("PNG")},
                                                   {QStringLiteral("jpg"), QStringLiteral("JPEG")},
                                                   {QStringLiteral("bmp"), QStringLiteral("BMP")}};
    const QList<QPair<QString, QString>> videos = {{QStringLiteral("mp4"), QStringLiteral("MP4")},
                                                   {QStringLiteral("mkv"), QStringLiteral("MKV")}};
    row(tr("Screenshots:"), m_settings.snapshotDir, m_settings.snapshotFormat, images, &m_snapshots);
    row(tr("Camera videos:"), m_settings.videoDir, m_settings.videoFormat, videos, &m_videos);
    row(tr("Screen recordings:"), m_settings.screenDir, m_settings.screenFormat, videos, &m_screen);
    auto *note = new QLabel(tr("Empty folder: the scan folder, where the files also appear in the file manager. "
                               "MKV files survive a crash during the recording; MP4 files play everywhere."),
                            w);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#666;"));
    f->addRow(note);
    return w;
}

} // namespace lensscan
