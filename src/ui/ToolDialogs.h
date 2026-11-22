// SPDX-License-Identifier: GPL-3.0-or-later
// More dialogs of the file manager: precise crop, splitting facing pages, processing
// scans again in another page mode, and the TIFF output options.
#pragma once

#include "export/Exporters.h"
#include "imaging/ColorModes.h"
#include "imaging/DocumentTools.h"
#include "imaging/ScanProcessor.h"

#include <QDialog>
#include <QImage>
#include <QStringList>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QSpinBox;

namespace lensscan {

// Margins in pixels for each side, the same for every page or, for books, one set for
// left-hand pages (1st, 3rd... selected) and one for right-hand pages.
class PreciseCropDialog : public QDialog {
    Q_OBJECT
public:
    PreciseCropDialog(const QStringList &files, QWidget *parent = nullptr);
    bool bookMode() const;
    bool cut() const;
    Margins margins(int pageIndex) const; // for the n-th selected page

private:
    void showPage(int index);
    void refresh();
    Margins read(int set) const;

    QStringList m_files;
    int m_index = 0;
    QImage m_page, m_sample;
    double m_scale = 1;
    QLabel *m_preview, *m_pageLabel, *m_sizeLabel;
    QRadioButton *m_single, *m_book, *m_cut, *m_white;
    QWidget *m_leftBox, *m_rightBox;
    QSpinBox *m_spin[2][4] = {}; // [left/right page][top, bottom, left, right]
};

class PageSplitDialog : public QDialog {
    Q_OBJECT
public:
    PageSplitDialog(int pageCount, QWidget *parent = nullptr);
    bool separateFiles() const;
    bool removeFingers() const;

private:
    QRadioButton *m_separate;
    QCheckBox *m_fingers;
};

class ReprocessDialog : public QDialog {
    Q_OBJECT
public:
    // withOriginal: how many of the pages have their camera picture kept.
    ReprocessDialog(int pageCount, int withOriginal, ColorMode colorMode, QWidget *parent = nullptr);
    PageMode pageMode() const;
    ColorMode colorMode() const;

private:
    QButtonGroup *m_modes;
    QComboBox *m_color;
};

class TiffDialog : public QDialog {
    Q_OBJECT
public:
    TiffDialog(int pageCount, QWidget *parent = nullptr);
    TiffOptions options() const;

private:
    QComboBox *m_compression, *m_color;
    QRadioButton *m_multi;
    QSpinBox *m_quality;
};

} // namespace lensscan
