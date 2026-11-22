// SPDX-License-Identifier: GPL-3.0-or-later
// Dialogs of the file manager's batch tools: page editor (crop / rotate), image
// adjustment, watermark, batch rename, blank page review and page order.
#pragma once

#include "imaging/BatchTools.h"
#include "imaging/Barcode.h"
#include "imaging/DocumentTools.h"

#include <QDialog>
#include <QIcon>
#include <QImage>
#include <QStringList>

#include <functional>

class QLabel;
class QListWidget;
class QTreeWidget;

namespace lensscan {

class PreviewWidget;

// Crop (drag a rectangle) and rotate one page. result() is the edited image.
class PageEditorDialog : public QDialog {
    Q_OBJECT
public:
    PageEditorDialog(const QImage &image, const QString &title, QWidget *parent = nullptr);
    QImage result() const { return m_image; }

private:
    void show(const QImage &img);
    PreviewWidget *m_view;
    QImage m_original, m_image;
};

class AdjustDialog : public QDialog {
    Q_OBJECT
public:
    AdjustDialog(const QImage &sample, int pageCount, QWidget *parent = nullptr);
    Adjustment adjustment() const { return m_adj; }

private:
    void refresh();
    QImage m_sample;
    QLabel *m_preview;
    Adjustment m_adj;
};

class WatermarkDialog : public QDialog {
    Q_OBJECT
public:
    WatermarkDialog(const QImage &sample, int pageCount, QWidget *parent = nullptr);
    Watermark watermark() const { return m_wm; }

private:
    void refresh();
    QImage m_sample;
    QImage m_logo;
    QLabel *m_preview;
    Watermark m_wm;
};

class RenameDialog : public QDialog {
    Q_OBJECT
public:
    RenameDialog(const QStringList &files, QWidget *parent = nullptr);
    QStringList newPaths() const;

private:
    void refresh();
    QStringList m_files;
    QString m_prefix;
    int m_start = 1, m_digits = 3;
    QLabel *m_example;
};

// Lists the suspected blank pages; checked ones are returned for deletion.
class BlankPagesDialog : public QDialog {
    Q_OBJECT
public:
    BlankPagesDialog(const QStringList &files, const std::function<QIcon(const QString &)> &thumb,
                     QWidget *parent = nullptr);
    QStringList checkedFiles() const;

private:
    QListWidget *m_list;
};

// Drag and drop the pages into the order of the document.
class PageOrderDialog : public QDialog {
    Q_OBJECT
public:
    PageOrderDialog(const QStringList &files, const std::function<QIcon(const QString &)> &thumb,
                    QWidget *parent = nullptr);
    QStringList orderedFiles() const;

private:
    QListWidget *m_list;
};

class HandwritingDialog : public QDialog {
    Q_OBJECT
public:
    HandwritingDialog(const QImage &sample, int pageCount, QWidget *parent = nullptr);
    HandwritingOptions options() const { return m_opt; }

private:
    void refresh();
    QImage m_sample;
    QLabel *m_preview;
    HandwritingOptions m_opt;
    bool m_showOriginal = false;
};

// Radii are in pixels of the full-size pages; the preview is scaled accordingly.
class PunchHoleDialog : public QDialog {
    Q_OBJECT
public:
    PunchHoleDialog(const QImage &sample, int pageCount, QWidget *parent = nullptr);
    int minRadius() const { return m_min; }
    int maxRadius() const { return m_max; }

private:
    void refresh();
    QImage m_sample;
    double m_scale = 1.0; // preview pixels per page pixel
    QLabel *m_preview, *m_found;
    int m_min = 15, m_max = 80;
};

struct FoundBarcode {
    QString file;
    BarcodeResult code;
};

// Results of the barcode reading: check rows, copy them or save them as CSV.
class BarcodeDialog : public QDialog {
    Q_OBJECT
public:
    BarcodeDialog(const QVector<FoundBarcode> &codes, QWidget *parent = nullptr);

private:
    QString checkedText(bool csv) const;
    QTreeWidget *m_tree;
};

} // namespace lensscan
