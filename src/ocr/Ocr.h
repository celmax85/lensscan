// SPDX-License-Identifier: GPL-3.0-or-later
// Text recognition with Tesseract: searchable PDF, plain text and Word (.docx) output.
#pragma once

#include "export/Exporters.h"

#include <QImage>
#include <QPair>
#include <QRect>
#include <QVector>
#include <QString>
#include <QStringList>

namespace lensscan {

// Directory holding *.traineddata (TESSDATA_PREFIX, the AppImage, or the distro path).
// Empty when Tesseract language data is not installed.
QString tessdataDir();

// Installed recognition languages ("eng", "fra", ...), without "osd" and "equ".
QStringList ocrAvailableLanguages();

// Default language string for this system: the UI language if installed, plus English.
QString ocrDefaultLanguages();

// Human-readable name for a Tesseract language code ("fra" -> "French").
QString ocrLanguageName(const QString &code);

// languages: Tesseract syntax, e.g. "fra+eng".
// PDF: each page keeps the original image with an invisible text layer on top.
bool ocrToPdf(const QStringList &files, const QString &outPath, const QString &languages, const QString &title,
              const ExportProgress &progress, QString *error);
// Plain UTF-8 text, pages separated by a form feed.
bool ocrToText(const QStringList &files, const QString &outPath, const QString &languages,
               const ExportProgress &progress, QString *error);
// Word document: one paragraph per text block, a page break between pages.
bool ocrToDocx(const QStringList &files, const QString &outPath, const QString &languages,
               const ExportProgress &progress, QString *error);

// Excel workbook (.xlsx): one sheet per page. Text lines become rows; columns come from
// the table's ruling lines when it has some, otherwise from text aligned in columns.
bool ocrToXlsx(const QStringList &files, const QString &outPath, const QString &languages,
               const ExportProgress &progress, QString *error);

// Clockwise rotation (0, 90, 180 or 270) that turns the text of the page upright, or
// -1 when it cannot be told (no osd.traineddata, too little text, low confidence).
int detectUprightRotation(const QImage &page);

// Building blocks, exposed for tests.
struct OcrWord {
    QRect box;
    QString text;
    int line = 0; // text line number on the page
};
// columnEdges: x of vertical ruling lines, sorted (empty = none found).
QVector<QStringList> tableFromWords(const QVector<OcrWord> &words, const QVector<int> &columnEdges);
QByteArray xlsxFromSheets(const QList<QPair<QString, QVector<QStringList>>> &sheets);
QByteArray docxFromPages(const QList<QStringList> &pages); // pages -> paragraphs
QByteArray storedZip(const QList<QPair<QString, QByteArray>> &entries);

} // namespace lensscan
