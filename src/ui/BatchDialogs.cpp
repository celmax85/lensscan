// SPDX-License-Identifier: GPL-3.0-or-later
#include "BatchDialogs.h"

#include "PreviewWidget.h"
#include "imaging/ScanProcessor.h"
#include "ui/Icons.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QSaveFile>
#include <QTreeWidget>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lensscan {

namespace {

constexpr int kPreview = 420;

QImage previewSample(const QImage &img)
{
    return img.isNull() ? img : img.scaled(kPreview * 2, kPreview * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QLabel *previewLabel(QWidget *parent)
{
    auto *l = new QLabel(parent);
    l->setFixedSize(kPreview, kPreview);
    l->setAlignment(Qt::AlignCenter);
    l->setStyleSheet(QStringLiteral("background:#e9ebef;border:1px solid #d0d3da;"));
    return l;
}

void setPreview(QLabel *label, const QImage &img)
{
    label->setPixmap(QPixmap::fromImage(
        img.scaled(kPreview - 8, kPreview - 8, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

QSlider *slider(int min, int max, int value, QWidget *parent)
{
    auto *s = new QSlider(Qt::Horizontal, parent);
    s->setRange(min, max);
    s->setValue(value);
    s->setMinimumWidth(200);
    return s;
}

QDialogButtonBox *okCancel(QDialog *d, const QString &okText)
{
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, d);
    box->button(QDialogButtonBox::Ok)->setText(okText);
    QObject::connect(box, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, d, &QDialog::reject);
    return box;
}

QListWidget *thumbList(QWidget *parent)
{
    auto *l = new QListWidget(parent);
    l->setViewMode(QListView::IconMode);
    l->setIconSize(QSize(120, 120));
    l->setGridSize(QSize(150, 168));
    l->setResizeMode(QListView::Adjust);
    l->setWordWrap(true);
    l->setMinimumSize(640, 420);
    return l;
}

} // namespace

// --- Page editor ---------------------------------------------------------------

PageEditorDialog::PageEditorDialog(const QImage &image, const QString &title, QWidget *parent)
    : QDialog(parent), m_original(image), m_image(image)
{
    setWindowTitle(tr("Edit page - %1").arg(title));
    resize(900, 720);
    auto *l = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("Drag a rectangle on the page to choose the area to keep."), this);
    l->addWidget(hint);
    m_view = new PreviewWidget(this);
    m_view->setSelectionEnabled(true);
    l->addWidget(m_view, 1);

    auto *bar = new QHBoxLayout;
    auto *rotL = new QPushButton(icon(IconId::RotateLeft), tr("Rotate left"), this);
    auto *rotR = new QPushButton(icon(IconId::RotateRight), tr("Rotate right"), this);
    auto *crop = new QPushButton(icon(IconId::Crop), tr("Crop to selection"), this);
    auto *reset = new QPushButton(tr("Undo all"), this);
    crop->setEnabled(false);
    connect(m_view, &PreviewWidget::selectionChanged, this,
            [crop](const QRectF &r) { crop->setEnabled(!r.isEmpty()); });
    connect(rotL, &QPushButton::clicked, this, [this] { show(rotateImage(m_image, -90)); });
    connect(rotR, &QPushButton::clicked, this, [this] { show(rotateImage(m_image, 90)); });
    connect(crop, &QPushButton::clicked, this, [this] { show(cropNormalized(m_image, m_view->selection())); });
    connect(reset, &QPushButton::clicked, this, [this] { show(m_original); });
    for (auto *b : {rotL, rotR, crop, reset})
        bar->addWidget(b);
    bar->addStretch(1);
    auto *box = okCancel(this, tr("Save"));
    bar->addWidget(box);
    l->addLayout(bar);
    show(m_image);
}

void PageEditorDialog::show(const QImage &img)
{
    m_image = img;
    m_view->setSelection({});
    m_view->setImage(img);
    m_view->setMessage(tr("%1 x %2 px").arg(img.width()).arg(img.height()));
}

// --- Adjust --------------------------------------------------------------------

AdjustDialog::AdjustDialog(const QImage &sample, int pageCount, QWidget *parent)
    : QDialog(parent), m_sample(previewSample(sample))
{
    setWindowTitle(tr("Brightness, contrast and sharpness"));
    auto *l = new QHBoxLayout(this);
    m_preview = previewLabel(this);
    l->addWidget(m_preview);
    auto *right = new QVBoxLayout;
    auto *form = new QFormLayout;
    auto *b = slider(-100, 100, 0, this), *c = slider(-100, 100, 0, this), *s = slider(0, 100, 0, this);
    form->addRow(tr("Brightness"), b);
    form->addRow(tr("Contrast"), c);
    form->addRow(tr("Sharpness"), s);
    right->addLayout(form);
    auto *reset = new QPushButton(tr("Reset"), this);
    connect(reset, &QPushButton::clicked, this, [b, c, s] {
        b->setValue(0);
        c->setValue(0);
        s->setValue(0);
    });
    right->addWidget(reset, 0, Qt::AlignLeft);
    right->addStretch(1);
    right->addWidget(new QLabel(tr("Applies to %n page(s).", nullptr, pageCount), this));
    right->addWidget(okCancel(this, tr("Apply")));
    l->addLayout(right);
    auto update = [this, b, c, s] {
        m_adj = {b->value(), c->value(), s->value()};
        refresh();
    };
    for (auto *sl : {b, c, s})
        connect(sl, &QSlider::valueChanged, this, update);
    refresh();
}

void AdjustDialog::refresh()
{
    setPreview(m_preview, adjustImage(m_sample, m_adj));
}

// --- Watermark -----------------------------------------------------------------

WatermarkDialog::WatermarkDialog(const QImage &sample, int pageCount, QWidget *parent)
    : QDialog(parent), m_sample(previewSample(sample))
{
    setWindowTitle(tr("Watermark"));
    m_wm.text = tr("CONFIDENTIAL");
    auto *l = new QHBoxLayout(this);
    m_preview = previewLabel(this);
    l->addWidget(m_preview);
    auto *right = new QVBoxLayout;
    auto *form = new QFormLayout;
    auto *kind = new QComboBox(this);
    kind->addItem(tr("Text"));
    kind->addItem(tr("Image (logo, signature, stamp)"));
    auto *text = new QLineEdit(m_wm.text, this);
    auto *imageButton = new QPushButton(tr("Choose an image..."), this);
    auto *imageWidth = new QSpinBox(this);
    imageWidth->setRange(2, 100);
    imageWidth->setSuffix(QStringLiteral(" %"));
    imageWidth->setValue(m_wm.imageWidthPercent);
    imageWidth->setToolTip(tr("Image width, as a percentage of the page width"));
    auto *pos = new QComboBox(this);
    const QList<QPair<WatermarkPosition, QString>> positions = {
        {WatermarkPosition::Diagonal, tr("Diagonal")},      {WatermarkPosition::Center, tr("Center")},
        {WatermarkPosition::Tiled, tr("Repeated")},         {WatermarkPosition::TopLeft, tr("Top left")},
        {WatermarkPosition::TopRight, tr("Top right")},     {WatermarkPosition::BottomLeft, tr("Bottom left")},
        {WatermarkPosition::BottomRight, tr("Bottom right")}};
    for (const auto &p : positions)
        pos->addItem(p.second, int(p.first));
    auto *size = new QSpinBox(this);
    size->setRange(1, 30);
    size->setSuffix(QStringLiteral(" %"));
    size->setValue(m_wm.sizePercent);
    size->setToolTip(tr("Text height, as a percentage of the page height"));
    auto *opacity = slider(5, 100, m_wm.opacity, this);
    auto *color = new QPushButton(this);
    auto paintColor = [color, this] {
        QPixmap pm(40, 14);
        pm.fill(m_wm.color);
        color->setIcon(QIcon(pm));
        color->setText(m_wm.color.name());
    };
    paintColor();
    connect(color, &QPushButton::clicked, this, [this, paintColor] {
        const QColor c = QColorDialog::getColor(m_wm.color, this, tr("Watermark color"));
        if (c.isValid()) {
            m_wm.color = c;
            paintColor();
            refresh();
        }
    });
    form->addRow(tr("Type"), kind);
    form->addRow(tr("Text"), text);
    form->addRow(tr("Image"), imageButton);
    form->addRow(tr("Position"), pos);
    form->addRow(tr("Size"), size);
    form->addRow(tr("Image width"), imageWidth);
    form->addRow(tr("Opacity"), opacity);
    form->addRow(tr("Color"), color);
    auto showKind = [form, kind, text, imageButton, size, imageWidth, color] {
        const bool img = kind->currentIndex() == 1;
        for (QWidget *w : {static_cast<QWidget *>(text), static_cast<QWidget *>(size), static_cast<QWidget *>(color)}) {
            w->setVisible(!img);
            form->labelForField(w)->setVisible(!img);
        }
        for (QWidget *w : {static_cast<QWidget *>(imageButton), static_cast<QWidget *>(imageWidth)}) {
            w->setVisible(img);
            form->labelForField(w)->setVisible(img);
        }
    };
    showKind();
    right->addLayout(form);
    right->addStretch(1);
    right->addWidget(new QLabel(tr("Applies to %n page(s).", nullptr, pageCount), this));
    auto *box = okCancel(this, tr("Apply"));
    right->addWidget(box);
    l->addLayout(right);
    auto update = [this, kind, text, pos, size, opacity, box, imageWidth] {
        if (kind->currentIndex() == 0)
            m_wm.image = QImage();
        else
            m_wm.image = m_logo;
        m_wm.imageWidthPercent = imageWidth->value();
        m_wm.text = text->text();
        m_wm.position = WatermarkPosition(pos->currentData().toInt());
        m_wm.sizePercent = size->value();
        m_wm.opacity = opacity->value();
        box->button(QDialogButtonBox::Ok)->setEnabled(kind->currentIndex() == 0 ? !m_wm.text.trimmed().isEmpty()
                                                                                 : !m_logo.isNull());
        refresh();
    };
    connect(text, &QLineEdit::textChanged, this, update);
    connect(kind, qOverload<int>(&QComboBox::currentIndexChanged), this, [showKind, update] {
        showKind();
        update();
    });
    connect(imageWidth, qOverload<int>(&QSpinBox::valueChanged), this, update);
    connect(imageButton, &QPushButton::clicked, this, [this, imageButton, update] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Watermark image"), QDir::homePath(),
                                                       tr("Images (*.png *.jpg *.jpeg *.bmp *.svg)"));
        if (f.isEmpty())
            return;
        QImage img(f);
        if (img.isNull())
            return;
        m_logo = img;
        imageButton->setText(QFileInfo(f).fileName());
        update();
    });
    connect(pos, qOverload<int>(&QComboBox::currentIndexChanged), this, update);
    connect(size, qOverload<int>(&QSpinBox::valueChanged), this, update);
    connect(opacity, &QSlider::valueChanged, this, update);
    refresh();
}

void WatermarkDialog::refresh()
{
    setPreview(m_preview, addWatermark(m_sample, m_wm));
}

// --- Rename --------------------------------------------------------------------

RenameDialog::RenameDialog(const QStringList &files, QWidget *parent) : QDialog(parent), m_files(files)
{
    setWindowTitle(tr("Rename"));
    m_prefix = tr("Page_");
    auto *l = new QVBoxLayout(this);
    l->addWidget(new QLabel(tr("New names: prefix + serial number, in the order shown in the list."), this));
    auto *form = new QFormLayout;
    auto *prefix = new QLineEdit(m_prefix, this);
    auto *start = new QSpinBox(this);
    start->setRange(0, 999999);
    start->setValue(m_start);
    auto *digits = new QSpinBox(this);
    digits->setRange(1, 9);
    digits->setValue(m_digits);
    form->addRow(tr("Prefix"), prefix);
    form->addRow(tr("Start at"), start);
    form->addRow(tr("Digits"), digits);
    l->addLayout(form);
    m_example = new QLabel(this);
    m_example->setStyleSheet(QStringLiteral("color:#5a6070;"));
    l->addWidget(m_example);
    auto *box = okCancel(this, tr("Rename"));
    l->addWidget(box);
    auto update = [this, prefix, start, digits, box] {
        m_prefix = prefix->text();
        m_start = start->value();
        m_digits = digits->value();
        static const QRegularExpression bad(QStringLiteral(R"([/\\:*?"<>|])"));
        box->button(QDialogButtonBox::Ok)->setEnabled(!m_prefix.contains(bad));
        refresh();
    };
    connect(prefix, &QLineEdit::textChanged, this, update);
    connect(start, qOverload<int>(&QSpinBox::valueChanged), this, update);
    connect(digits, qOverload<int>(&QSpinBox::valueChanged), this, update);
    refresh();
}

QStringList RenameDialog::newPaths() const
{
    return renamedPaths(m_files, m_prefix, m_start, m_digits);
}

void RenameDialog::refresh()
{
    const QStringList p = newPaths();
    QStringList ex;
    for (int i = 0; i < std::min<qsizetype>(3, p.size()); ++i)
        ex << QFileInfo(m_files[i]).fileName() + QStringLiteral("  →  ") + QFileInfo(p[i]).fileName();
    if (p.size() > 3)
        ex << QStringLiteral("…");
    m_example->setText(ex.join(QLatin1Char('\n')));
}

// --- Blank pages ---------------------------------------------------------------

BlankPagesDialog::BlankPagesDialog(const QStringList &files, const std::function<QIcon(const QString &)> &thumb,
                                   QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Blank page detection"));
    auto *l = new QVBoxLayout(this);
    l->addWidget(new QLabel(tr("Suspected blank pages: %1. Uncheck the pages to keep.").arg(files.size()), this));
    m_list = thumbList(this);
    for (const QString &f : files) {
        auto *it = new QListWidgetItem(thumb(f), QFileInfo(f).fileName(), m_list);
        it->setData(Qt::UserRole, f);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
    }
    l->addWidget(m_list, 1);
    l->addWidget(okCancel(this, tr("Move checked pages to the trash")));
}

QStringList BlankPagesDialog::checkedFiles() const
{
    QStringList out;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->checkState() == Qt::Checked)
            out << m_list->item(i)->data(Qt::UserRole).toString();
    return out;
}

// --- Page order ----------------------------------------------------------------

PageOrderDialog::PageOrderDialog(const QStringList &files, const std::function<QIcon(const QString &)> &thumb,
                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Page order"));
    auto *l = new QVBoxLayout(this);
    l->addWidget(new QLabel(tr("Drag the pages into the order of the document."), this));
    m_list = thumbList(this);
    m_list->setMovement(QListView::Snap);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const QString &f : files) {
        auto *it = new QListWidgetItem(thumb(f), QFileInfo(f).fileName(), m_list);
        it->setData(Qt::UserRole, f);
    }
    l->addWidget(m_list, 1);
    auto *bar = new QHBoxLayout;
    auto *byName = new QPushButton(tr("Sort by name"), this);
    auto *reverse = new QPushButton(tr("Reverse"), this);
    connect(byName, &QPushButton::clicked, this, [this] { m_list->sortItems(); });
    connect(reverse, &QPushButton::clicked, this, [this] {
        QList<QListWidgetItem *> items;
        while (m_list->count())
            items.prepend(m_list->takeItem(0));
        for (auto *it : items)
            m_list->addItem(it);
    });
    bar->addWidget(byName);
    bar->addWidget(reverse);
    bar->addStretch(1);
    bar->addWidget(okCancel(this, tr("Continue")));
    l->addLayout(bar);
}

QStringList PageOrderDialog::orderedFiles() const
{
    QStringList out;
    for (int i = 0; i < m_list->count(); ++i)
        out << m_list->item(i)->data(Qt::UserRole).toString();
    return out;
}

// --- Handwriting removal ---------------------------------------------------------

HandwritingDialog::HandwritingDialog(const QImage &sample, int pageCount, QWidget *parent)
    : QDialog(parent), m_sample(previewSample(sample))
{
    setWindowTitle(tr("Erase handwriting"));
    auto *l = new QHBoxLayout(this);
    m_preview = previewLabel(this);
    l->addWidget(m_preview);
    auto *right = new QVBoxLayout;
    auto *info = new QLabel(tr("Removes pen strokes of the chosen ink colors and keeps the black printed text. "
                               "Black pen cannot be told apart from print and is kept."),
                            this);
    info->setWordWrap(true);
    info->setMaximumWidth(300);
    right->addWidget(info);
    auto *blue = new QCheckBox(tr("Blue and purple ink"), this);
    blue->setChecked(m_opt.blue);
    auto *red = new QCheckBox(tr("Red ink (also red stamps)"), this);
    red->setChecked(m_opt.red);
    auto *green = new QCheckBox(tr("Green ink"), this);
    green->setChecked(m_opt.green);
    for (auto *c : {blue, red, green})
        right->addWidget(c);
    auto *form = new QFormLayout;
    auto *strength = slider(0, 100, m_opt.strength, this);
    strength->setToolTip(tr("Higher values also remove paler strokes, but may touch colored print"));
    form->addRow(tr("Strength"), strength);
    right->addLayout(form);
    auto *clean = new QCheckBox(tr("Clean the background (tinted paper only)"), this);
    clean->setToolTip(tr("Makes a tinted or unevenly lit paper white after erasing"));
    right->addWidget(clean);
    auto *original = new QPushButton(tr("Hold to show the original"), this);
    connect(original, &QPushButton::pressed, this, [this] {
        m_showOriginal = true;
        refresh();
    });
    connect(original, &QPushButton::released, this, [this] {
        m_showOriginal = false;
        refresh();
    });
    right->addWidget(original, 0, Qt::AlignLeft);
    right->addStretch(1);
    right->addWidget(new QLabel(tr("Applies to %n page(s).", nullptr, pageCount), this));
    right->addWidget(okCancel(this, tr("Apply")));
    l->addLayout(right);
    auto update = [this, blue, red, green, strength, clean] {
        m_opt.blue = blue->isChecked();
        m_opt.red = red->isChecked();
        m_opt.green = green->isChecked();
        m_opt.strength = strength->value();
        m_opt.cleanBackground = clean->isChecked();
        refresh();
    };
    for (auto *c : {blue, red, green, clean})
        connect(c, &QCheckBox::toggled, this, update);
    connect(strength, &QSlider::sliderReleased, this, update);
    connect(strength, &QSlider::valueChanged, this, [strength, update] {
        if (!strength->isSliderDown())
            update();
    });
    refresh();
}

void HandwritingDialog::refresh()
{
    setPreview(m_preview, m_showOriginal ? m_sample : eraseHandwriting(m_sample, m_opt));
}

// --- Punch holes ------------------------------------------------------------------

PunchHoleDialog::PunchHoleDialog(const QImage &sample, int pageCount, QWidget *parent)
    : QDialog(parent), m_sample(previewSample(sample))
{
    setWindowTitle(tr("Fill punch holes"));
    if (!sample.isNull() && sample.width() > 0)
        m_scale = double(m_sample.width()) / sample.width();
    // Default radius range: 2 to 5 mm holes on an A4-sized page.
    if (!sample.isNull()) {
        const double pxPerMm = std::max(sample.width(), sample.height()) / 297.0;
        m_min = std::max(3, int(2 * pxPerMm));
        m_max = std::max(m_min + 2, int(5.5 * pxPerMm));
    }
    auto *l = new QHBoxLayout(this);
    m_preview = previewLabel(this);
    l->addWidget(m_preview);
    auto *right = new QVBoxLayout;
    auto *info = new QLabel(tr("Fills the round binder holes near the page edges with the paper color."), this);
    info->setWordWrap(true);
    info->setMaximumWidth(300);
    right->addWidget(info);
    auto *form = new QFormLayout;
    auto *minR = new QSpinBox(this), *maxR = new QSpinBox(this);
    for (auto *sp : {minR, maxR}) {
        sp->setRange(2, 2000);
        sp->setSuffix(tr(" px"));
    }
    minR->setValue(m_min);
    maxR->setValue(m_max);
    form->addRow(tr("Minimum radius"), minR);
    form->addRow(tr("Maximum radius"), maxR);
    right->addLayout(form);
    m_found = new QLabel(this);
    right->addWidget(m_found);
    right->addStretch(1);
    right->addWidget(new QLabel(tr("Applies to %n page(s).", nullptr, pageCount), this));
    right->addWidget(okCancel(this, tr("Apply")));
    l->addLayout(right);
    auto update = [this, minR, maxR] {
        m_min = std::min(minR->value(), maxR->value());
        m_max = std::max(minR->value(), maxR->value());
        refresh();
    };
    connect(minR, qOverload<int>(&QSpinBox::valueChanged), this, update);
    connect(maxR, qOverload<int>(&QSpinBox::valueChanged), this, update);
    refresh();
}

void PunchHoleDialog::refresh()
{
    int n = 0;
    const QImage out = fillPunchHoles(m_sample, std::max(2, int(m_min * m_scale)), std::max(3, int(m_max * m_scale + 0.5)), &n);
    setPreview(m_preview, out);
    m_found->setText(tr("Holes found on this page: %1").arg(n));
}

// --- Barcodes ---------------------------------------------------------------------

BarcodeDialog::BarcodeDialog(const QVector<FoundBarcode> &codes, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Barcodes and QR codes"));
    resize(760, 420);
    auto *l = new QVBoxLayout(this);
    l->addWidget(new QLabel(tr("%n code(s) found.", nullptr, int(codes.size())), this));
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Page"), tr("Type"), tr("Content")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const FoundBarcode &c : codes) {
        auto *it = new QTreeWidgetItem(m_tree, {QFileInfo(c.file).fileName(), c.code.type, c.code.text});
        it->setCheckState(0, Qt::Checked);
        it->setToolTip(2, c.code.text);
    }
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    l->addWidget(m_tree, 1);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *copy = box->addButton(tr("Copy checked"), QDialogButtonBox::ActionRole);
    auto *save = box->addButton(tr("Save as CSV..."), QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(checkedText(false)); });
    connect(save, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save as CSV"), QDir::homePath() + QStringLiteral("/barcodes.csv"),
                                                          tr("CSV files (*.csv)"));
        if (path.isEmpty())
            return;
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(checkedText(true).toUtf8()) < 0 || !f.commit())
            QMessageBox::warning(this, tr("Save as CSV"), tr("Cannot write %1").arg(path));
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(box);
}

QString BarcodeDialog::checkedText(bool csv) const
{
    auto quote = [](QString v) { return QLatin1Char('"') + v.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"'); };
    QStringList lines;
    if (csv)
        lines << QStringLiteral("page,type,content");
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *it = m_tree->topLevelItem(i);
        if (it->checkState(0) != Qt::Checked)
            continue;
        lines << (csv ? QStringLiteral("%1,%2,%3").arg(quote(it->text(0)), quote(it->text(1)), quote(it->text(2)))
                      : it->text(2));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace lensscan
