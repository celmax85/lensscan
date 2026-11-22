// SPDX-License-Identifier: GPL-3.0-or-later
#include "ToolDialogs.h"

#include "ui/Icons.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lensscan {

namespace {

constexpr int kPreview = 440;

QDialogButtonBox *okCancel(QDialog *d, const QString &okText)
{
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, d);
    box->button(QDialogButtonBox::Ok)->setText(okText);
    QObject::connect(box, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, d, &QDialog::reject);
    return box;
}

QToolButton *navButton(IconId id, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icon(id));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

} // namespace

// ---------------------------------------------------------------------------

PreciseCropDialog::PreciseCropDialog(const QStringList &files, QWidget *parent) : QDialog(parent), m_files(files)
{
    setWindowTitle(tr("Precise crop"));
    auto *top = new QHBoxLayout(this);

    auto *left = new QVBoxLayout;
    m_preview = new QLabel(this);
    m_preview->setFixedSize(kPreview, kPreview);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background:#e9ebef;border:1px solid #d0d3da;"));
    left->addWidget(m_preview);
    auto *nav = new QHBoxLayout;
    auto *first = navButton(IconId::First, tr("First page"), this);
    auto *prev = navButton(IconId::Previous, tr("Previous page"), this);
    auto *next = navButton(IconId::Next, tr("Next page"), this);
    auto *last = navButton(IconId::Last, tr("Last page"), this);
    connect(first, &QToolButton::clicked, this, [this] { showPage(0); });
    connect(prev, &QToolButton::clicked, this, [this] { showPage(m_index - 1); });
    connect(next, &QToolButton::clicked, this, [this] { showPage(m_index + 1); });
    connect(last, &QToolButton::clicked, this, [this] { showPage(int(m_files.size()) - 1); });
    m_pageLabel = new QLabel(this);
    m_pageLabel->setAlignment(Qt::AlignCenter);
    nav->addStretch(1);
    nav->addWidget(first);
    nav->addWidget(prev);
    nav->addWidget(m_pageLabel);
    nav->addWidget(next);
    nav->addWidget(last);
    nav->addStretch(1);
    left->addLayout(nav);
    m_sizeLabel = new QLabel(this);
    m_sizeLabel->setAlignment(Qt::AlignCenter);
    left->addWidget(m_sizeLabel);
    top->addLayout(left);

    auto *right = new QVBoxLayout;
    auto *kind = new QGroupBox(tr("Pages"), this);
    auto *kl = new QVBoxLayout(kind);
    m_single = new QRadioButton(tr("Same margins on every page"), kind);
    m_book = new QRadioButton(tr("Book: left-hand and right-hand pages mirrored"), kind);
    m_book->setToolTip(tr("The 1st, 3rd, 5th... selected pages are left-hand pages, the others right-hand pages"));
    m_single->setChecked(true);
    kl->addWidget(m_single);
    kl->addWidget(m_book);
    right->addWidget(kind);

    const QString sides[4] = {tr("Top:"), tr("Bottom:"), tr("Left:"), tr("Right:")};
    QWidget *boxes[2];
    for (int set = 0; set < 2; ++set) {
        auto *g = new QGroupBox(set == 0 ? tr("Margins to remove (pixels)") : tr("Right-hand pages (pixels)"), this);
        auto *gl = new QFormLayout(g);
        for (int k = 0; k < 4; ++k) {
            m_spin[set][k] = new QSpinBox(g);
            m_spin[set][k]->setRange(0, 20000);
            m_spin[set][k]->setSuffix(tr(" px"));
            connect(m_spin[set][k], qOverload<int>(&QSpinBox::valueChanged), this, &PreciseCropDialog::refresh);
            gl->addRow(sides[k], m_spin[set][k]);
        }
        boxes[set] = g;
        right->addWidget(g);
    }
    m_leftBox = boxes[0];
    m_rightBox = boxes[1];
    // Right-hand pages start as the mirror of the left-hand ones.
    for (int k = 0; k < 4; ++k)
        connect(m_spin[0][k], qOverload<int>(&QSpinBox::valueChanged), this, [this, k](int v) {
            if (m_rightBox->isEnabled())
                return;
            const int mirror = k == 2 ? 3 : k == 3 ? 2 : k;
            m_spin[1][mirror]->setValue(v);
        });
    m_rightBox->setEnabled(false);
    connect(m_book, &QRadioButton::toggled, this, [this](bool on) {
        m_rightBox->setEnabled(on);
        static_cast<QGroupBox *>(m_leftBox)->setTitle(on ? tr("Left-hand pages (pixels)") : tr("Margins to remove (pixels)"));
        refresh();
    });

    auto *outside = new QGroupBox(tr("Outside the margins"), this);
    auto *ol = new QVBoxLayout(outside);
    m_cut = new QRadioButton(tr("Cut away"), outside);
    m_white = new QRadioButton(tr("Fill with white (keep the page size)"), outside);
    m_cut->setChecked(true);
    ol->addWidget(m_cut);
    ol->addWidget(m_white);
    connect(m_cut, &QRadioButton::toggled, this, &PreciseCropDialog::refresh);
    right->addWidget(outside);
    right->addStretch(1);
    right->addWidget(okCancel(this, tr("Crop %n page(s)", nullptr, int(files.size()))));
    top->addLayout(right);

    showPage(0);
}

bool PreciseCropDialog::bookMode() const { return m_book->isChecked(); }
bool PreciseCropDialog::cut() const { return m_cut->isChecked(); }

Margins PreciseCropDialog::read(int set) const
{
    return {m_spin[set][0]->value(), m_spin[set][1]->value(), m_spin[set][2]->value(), m_spin[set][3]->value()};
}

Margins PreciseCropDialog::margins(int pageIndex) const
{
    return read(bookMode() && pageIndex % 2 == 1 ? 1 : 0);
}

void PreciseCropDialog::showPage(int index)
{
    if (m_files.isEmpty())
        return;
    m_index = std::clamp(index, 0, int(m_files.size()) - 1);
    m_page = loadScan(m_files[m_index]);
    m_sample = m_page.scaled(kPreview - 8, kPreview - 8, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_scale = m_page.isNull() ? 1.0 : double(m_sample.width()) / m_page.width();
    for (auto &set : m_spin)
        for (QSpinBox *s : set)
            s->setMaximum(std::max(0, std::max(m_page.width(), m_page.height()) - 16));
    refresh();
}

void PreciseCropDialog::refresh()
{
    if (m_page.isNull())
        return;
    const bool rightPage = bookMode() && m_index % 2 == 1;
    m_pageLabel->setText(QStringLiteral("%1 / %2").arg(m_index + 1).arg(m_files.size()) +
                         (bookMode() ? (rightPage ? tr(" (right-hand page)") : tr(" (left-hand page)")) : QString()));
    const Margins m = margins(m_index);
    QImage view = m_sample.convertToFormat(QImage::Format_RGB32);
    QPainter p(&view);
    const QRectF keep(m.left * m_scale, m.top * m_scale, view.width() - (m.left + m.right) * m_scale,
                      view.height() - (m.top + m.bottom) * m_scale);
    QRegion outside(view.rect());
    outside -= QRegion(keep.toAlignedRect());
    for (const QRect &r : outside)
        p.fillRect(r, cut() ? QColor(229, 57, 53, 110) : QColor(255, 255, 255, 230));
    p.setPen(QPen(QColor(0xe5, 0x39, 0x35), 1.5, Qt::DashLine));
    p.drawRect(keep);
    p.end();
    m_preview->setPixmap(QPixmap::fromImage(view));
    const QSize after = cut() ? QSize(std::max(16, m_page.width() - m.left - m.right),
                                      std::max(16, m_page.height() - m.top - m.bottom))
                              : m_page.size();
    m_sizeLabel->setText(tr("%1 — original size: %2 × %3, after: %4 × %5")
                             .arg(QFileInfo(m_files[m_index]).fileName())
                             .arg(m_page.width())
                             .arg(m_page.height())
                             .arg(after.width())
                             .arg(after.height()));
}

// ---------------------------------------------------------------------------

PageSplitDialog::PageSplitDialog(int pageCount, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Split facing pages"));
    auto *l = new QVBoxLayout(this);
    auto *info = new QLabel(tr("Each selected image showing two facing pages (an open book, a magazine, a double "
                               "page) is cut in two. Open books are also flattened."),
                            this);
    info->setWordWrap(true);
    info->setMinimumWidth(420);
    l->addWidget(info);
    auto *g = new QGroupBox(tr("Output"), this);
    auto *gl = new QVBoxLayout(g);
    m_separate = new QRadioButton(tr("One file per page (replaces the double page)"), g);
    auto *merged = new QRadioButton(tr("Both pages in one file, straightened"), g);
    const bool sep = QSettings().value(QStringLiteral("split/separate"), true).toBool();
    m_separate->setChecked(sep);
    merged->setChecked(!sep);
    gl->addWidget(m_separate);
    gl->addWidget(merged);
    l->addWidget(g);
    m_fingers = new QCheckBox(tr("Erase fingers holding the pages"), this);
    m_fingers->setChecked(QSettings().value(QStringLiteral("split/fingers"), true).toBool());
    l->addWidget(m_fingers);
    l->addWidget(okCancel(this, tr("Split %n image(s)", nullptr, pageCount)));
    connect(this, &QDialog::accepted, this, [this] {
        QSettings().setValue(QStringLiteral("split/separate"), m_separate->isChecked());
        QSettings().setValue(QStringLiteral("split/fingers"), m_fingers->isChecked());
    });
}

bool PageSplitDialog::separateFiles() const { return m_separate->isChecked(); }
bool PageSplitDialog::removeFingers() const { return m_fingers->isChecked(); }

// ---------------------------------------------------------------------------

ReprocessDialog::ReprocessDialog(int pageCount, int withOriginal, ColorMode colorMode, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Process again"));
    auto *l = new QVBoxLayout(this);
    auto *info = new QLabel(this);
    info->setWordWrap(true);
    info->setMinimumWidth(440);
    if (withOriginal == pageCount)
        info->setText(tr("The selected pages are made again from the camera pictures they were scanned from."));
    else if (withOriginal == 0)
        info->setText(tr("No camera picture was kept for these pages (imported, or scanned with Settings > General > "
                         "\"Keep the camera pictures\" off): they are processed again from the pages themselves."));
    else
        info->setText(tr("%1 of the %2 pages are made again from their camera picture; the others, which have "
                         "none, from the pages themselves.")
                          .arg(withOriginal)
                          .arg(pageCount));
    l->addWidget(info);
    auto *g = new QGroupBox(tr("Page handling"), this);
    auto *gl = new QVBoxLayout(g);
    m_modes = new QButtonGroup(this);
    const struct {
        PageMode mode;
        QString text, tip;
    } modes[] = {
        {PageMode::FlatPage, tr("Flat page: find the page, crop and straighten it"), QString()},
        {PageMode::Book, tr("Curved book: flatten and split the two pages"), QString()},
        {PageMode::MultiDocument, tr("Multiple documents: one file per document"), QString()},
        {PageMode::Original, tr("Original: the whole camera picture, not cropped"),
         tr("Only possible for pages whose camera picture was kept")},
    };
    for (const auto &m : modes) {
        auto *r = new QRadioButton(m.text, g);
        r->setToolTip(m.tip);
        m_modes->addButton(r, int(m.mode));
        gl->addWidget(r);
    }
    m_modes->button(int(PageMode::FlatPage))->setChecked(true);
    m_modes->button(int(PageMode::Original))->setEnabled(withOriginal > 0);
    l->addWidget(g);
    auto *f = new QFormLayout;
    m_color = new QComboBox(this);
    const QList<QPair<ColorMode, QString>> colors = {
        {ColorMode::None, tr("No change")},
        {ColorMode::Auto, tr("Auto optimize")},       {ColorMode::Color, tr("Color")},
        {ColorMode::BlackWhite, tr("Black and white")}, {ColorMode::Gray, tr("Grayscale")},
        {ColorMode::WhitePaperStamp, tr("White paper and stamps")}, {ColorMode::Watermark, tr("ID / watermark")},
    };
    for (const auto &c : colors)
        m_color->addItem(c.second, int(c.first));
    m_color->setCurrentIndex(std::max(0, m_color->findData(int(colorMode))));
    f->addRow(tr("Color mode:"), m_color);
    l->addLayout(f);
    l->addWidget(okCancel(this, tr("Process %n page(s)", nullptr, pageCount)));
}

PageMode ReprocessDialog::pageMode() const { return PageMode(m_modes->checkedId()); }
ColorMode ReprocessDialog::colorMode() const { return ColorMode(m_color->currentData().toInt()); }

// ---------------------------------------------------------------------------

TiffDialog::TiffDialog(int pageCount, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("TIFF options"));
    QSettings s;
    auto *l = new QVBoxLayout(this);
    auto *f = new QFormLayout;
    m_compression = new QComboBox(this);
    m_compression->addItem(tr("Automatic: Group 4 for black and white pages, JPEG for the others"),
                           int(TiffOptions::Compression::Auto));
    m_compression->addItem(tr("None (largest files)"), int(TiffOptions::Compression::None));
    m_compression->addItem(tr("LZW (lossless)"), int(TiffOptions::Compression::Lzw));
    m_compression->addItem(tr("JPEG (small files)"), int(TiffOptions::Compression::Jpeg));
    m_compression->addItem(tr("Group 4 (black and white, smallest files)"), int(TiffOptions::Compression::Group4));
    m_compression->setCurrentIndex(std::max(0, m_compression->findData(s.value(QStringLiteral("tiff/compression"), 0).toInt())));
    f->addRow(tr("Compression:"), m_compression);
    m_color = new QComboBox(this);
    m_color->addItem(tr("Keep the colors"), false);
    m_color->addItem(tr("Grayscale"), true);
    m_color->setCurrentIndex(s.value(QStringLiteral("tiff/gray"), false).toBool() ? 1 : 0);
    f->addRow(tr("Colors:"), m_color);
    m_quality = new QSpinBox(this);
    m_quality->setRange(30, 100);
    m_quality->setValue(s.value(QStringLiteral("tiff/quality"), 90).toInt());
    f->addRow(tr("JPEG quality:"), m_quality);
    l->addLayout(f);
    auto *g = new QGroupBox(tr("Output"), this);
    auto *gl = new QVBoxLayout(g);
    m_multi = new QRadioButton(tr("One multi-page file"), g);
    auto *single = new QRadioButton(tr("One file per page"), g);
    const bool multi = s.value(QStringLiteral("tiff/multiPage"), true).toBool();
    m_multi->setChecked(multi);
    single->setChecked(!multi);
    gl->addWidget(m_multi);
    gl->addWidget(single);
    l->addWidget(g);
    const auto update = [this] {
        const auto c = TiffOptions::Compression(m_compression->currentData().toInt());
        m_quality->setEnabled(c == TiffOptions::Compression::Jpeg || c == TiffOptions::Compression::Auto);
        m_color->setEnabled(c != TiffOptions::Compression::Group4);
    };
    connect(m_compression, qOverload<int>(&QComboBox::currentIndexChanged), this, update);
    update();
    l->addWidget(okCancel(this, tr("Save %n page(s)", nullptr, pageCount)));
    connect(this, &QDialog::accepted, this, [this] {
        QSettings s;
        s.setValue(QStringLiteral("tiff/compression"), m_compression->currentData().toInt());
        s.setValue(QStringLiteral("tiff/gray"), m_color->currentData().toBool());
        s.setValue(QStringLiteral("tiff/quality"), m_quality->value());
        s.setValue(QStringLiteral("tiff/multiPage"), m_multi->isChecked());
    });
}

TiffOptions TiffDialog::options() const
{
    TiffOptions o;
    o.compression = TiffOptions::Compression(m_compression->currentData().toInt());
    o.gray = m_color->currentData().toBool();
    o.multiPage = m_multi->isChecked();
    o.jpegQuality = m_quality->value();
    return o;
}

} // namespace lensscan
