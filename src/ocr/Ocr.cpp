// SPDX-License-Identifier: GPL-3.0-or-later
#include "Ocr.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>

#include <allheaders.h>
#include <tesseract/baseapi.h>
#include <tesseract/renderer.h>

#include <opencv2/imgproc.hpp>
#include <zlib.h>

#include <algorithm>
#include <limits>
#include <memory>

namespace lensscan {

namespace {

struct PixDeleter {
    void operator()(Pix *p) const { pixDestroy(&p); }
};
using PixPtr = std::unique_ptr<Pix, PixDeleter>;

// The camera writes no resolution. A scan is about one page, so assume the long side
// is an A4/Letter long side (11.7 in); Tesseract and the PDF page size both need it.
int estimateDpi(const Pix *pix)
{
    const int stored = pixGetXRes(const_cast<Pix *>(pix));
    if (stored >= 70 && stored <= 1200)
        return stored; // written by LensScan (Settings > General > Resolution)
    const int longSide = std::max(pixGetWidth(const_cast<Pix *>(pix)), pixGetHeight(const_cast<Pix *>(pix)));
    return std::clamp(int(longSide / 11.7), 150, 600);
}

class Engine
{
public:
    bool init(const QString &languages, QString *error)
    {
        m_dir = tessdataDir();
        if (m_dir.isEmpty()) {
            *error = QCoreApplication::translate("Ocr", "Tesseract language data not found. Install tesseract-ocr and a language package "
                        "(for example tesseract-ocr-fra).");
            return false;
        }
        const QByteArray dir = QFile::encodeName(m_dir), lang = languages.toUtf8();
        if (m_api.Init(dir.constData(), lang.constData(), tesseract::OEM_LSTM_ONLY) != 0 &&
            m_api.Init(dir.constData(), lang.constData()) != 0) {
            *error = QCoreApplication::translate("Ocr", "Cannot load the OCR languages \"%1\" from %2.").arg(languages, m_dir);
            return false;
        }
        m_api.SetPageSegMode(tesseract::PSM_AUTO);
        // Orientation detection is optional (needs osd.traineddata).
        if (QFile::exists(m_dir + QStringLiteral("/osd.traineddata")) &&
            m_osd.Init(dir.constData(), "osd", tesseract::OEM_TESSERACT_ONLY) == 0) {
            m_osd.SetPageSegMode(tesseract::PSM_OSD_ONLY);
            m_haveOsd = true;
        }
        return true;
    }

    // Loads a page and turns it upright. *rotated tells whether it differs from the file.
    PixPtr load(const QString &file, QString *error, bool *rotated = nullptr)
    {
        PixPtr pix(pixRead(QFile::encodeName(file).constData()));
        if (!pix) {
            *error = QCoreApplication::translate("Ocr", "Cannot read %1").arg(file);
            return nullptr;
        }
        if (rotated)
            *rotated = false;
        const int dpi = estimateDpi(pix.get());
        pixSetResolution(pix.get(), dpi, dpi);
        if (m_haveOsd) {
            m_osd.SetImage(pix.get());
            m_osd.SetSourceResolution(estimateDpi(pix.get()));
            int deg = 0;
            float degConf = 0, scriptConf = 0;
            const char *script = nullptr;
            const bool ok = m_osd.DetectOrientationScript(&deg, &degConf, &script, &scriptConf);
            if (ok && deg % 360 != 0 && degConf > 2.0f) {
                // deg is the counter-clockwise rotation of the page; pixRotateOrth turns
                // clockwise by quarter turns, so turn by the complement.
                PixPtr upright(pixRotateOrth(pix.get(), (4 - (deg / 90) % 4) % 4));
                if (upright) {
                    pix = std::move(upright);
                    if (rotated)
                        *rotated = true;
                }
            }
            m_osd.Clear();
        }
        return pix;
    }

    tesseract::TessBaseAPI &api() { return m_api; }
    const QString &dir() const { return m_dir; }

    ~Engine()
    {
        m_api.End();
        if (m_haveOsd)
            m_osd.End();
    }

private:
    tesseract::TessBaseAPI m_api, m_osd;
    bool m_haveOsd = false;
    QString m_dir;
};

} // namespace

int detectUprightRotation(const QImage &page)
{
    const QString dir = tessdataDir();
    if (dir.isEmpty() || !QFile::exists(dir + QStringLiteral("/osd.traineddata")) || page.isNull())
        return -1;
    QImage gray = page.convertToFormat(QImage::Format_Grayscale8);
    if (std::max(gray.width(), gray.height()) > 2400)
        gray = gray.scaled(2400, 2400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    PixPtr pix(pixCreate(gray.width(), gray.height(), 8));
    if (!pix)
        return -1;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *s = gray.constScanLine(y);
        l_uint32 *line = pixGetData(pix.get()) + y * pixGetWpl(pix.get());
        for (int x = 0; x < gray.width(); ++x)
            SET_DATA_BYTE(line, x, s[x]);
    }
    const int dpi = estimateDpi(pix.get());
    pixSetResolution(pix.get(), dpi, dpi);
    tesseract::TessBaseAPI osd;
    const QByteArray d = QFile::encodeName(dir);
    if (osd.Init(d.constData(), "osd", tesseract::OEM_TESSERACT_ONLY) != 0)
        return -1;
    osd.SetPageSegMode(tesseract::PSM_OSD_ONLY);
    osd.SetImage(pix.get());
    osd.SetSourceResolution(dpi);
    int deg = 0;
    float degConf = 0, scriptConf = 0;
    const char *script = nullptr;
    const bool ok = osd.DetectOrientationScript(&deg, &degConf, &script, &scriptConf);
    osd.End();
    if (!ok || degConf < 2.0f)
        return -1;
    // deg is the counter-clockwise rotation of the page.
    return (360 - deg % 360) % 360;
}

namespace {

QString utf8(char *s)
{
    const QString out = QString::fromUtf8(s ? s : "");
    delete[] s;
    return out;
}

// Paragraph texts of the recognized page, lines joined, end-of-line hyphens removed.
QStringList paragraphs(tesseract::TessBaseAPI &api)
{
    QStringList out;
    std::unique_ptr<tesseract::ResultIterator> it(api.GetIterator());
    if (!it)
        return out;
    do {
        QString para = utf8(it->GetUTF8Text(tesseract::RIL_PARA)).trimmed();
        if (para.isEmpty())
            continue;
        QStringList lines = para.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QString joined;
        for (QString line : lines) {
            line = line.trimmed();
            if (joined.endsWith(QLatin1Char('-')) && !line.isEmpty() && line.at(0).isLower())
                joined.chop(1);
            else if (!joined.isEmpty())
                joined += QLatin1Char(' ');
            joined += line;
        }
        out << joined;
    } while (it->Next(tesseract::RIL_PARA));
    return out;
}

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        *error = QCoreApplication::translate("Ocr", "Cannot write %1").arg(path);
        return false;
    }
    return true;
}

QString xmlEscape(const QString &s)
{
    QString out = s.toHtmlEscaped();
    // Drop control characters that XML 1.0 forbids.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](QChar c) { return c.unicode() < 0x20 && c != QLatin1Char('\t'); }),
              out.end());
    return out;
}

void put16(QByteArray &b, quint16 v)
{
    b.append(char(v & 0xff)).append(char(v >> 8));
}
void put32(QByteArray &b, quint32 v)
{
    put16(b, quint16(v & 0xffff));
    put16(b, quint16(v >> 16));
}

} // namespace

QString tessdataDir()
{
    QStringList candidates;
    const QString env = qEnvironmentVariable("TESSDATA_PREFIX");
    if (!env.isEmpty())
        candidates << env << env + QStringLiteral("/tessdata");
    const QString appdir = qEnvironmentVariable("APPDIR");
    if (!appdir.isEmpty())
        candidates << appdir + QStringLiteral("/usr/share/tessdata");
    candidates << QStringLiteral("/usr/share/tesseract-ocr/5/tessdata") // Debian, Ubuntu >= 23.04
               << QStringLiteral("/usr/share/tesseract-ocr/4.00/tessdata") // Ubuntu 22.04, Debian 11
               << QStringLiteral("/usr/share/tesseract/tessdata")          // Fedora
               << QStringLiteral("/usr/share/tessdata")                    // openSUSE, Arch
               << QStringLiteral("/usr/local/share/tessdata");
    for (const QString &d : candidates)
        if (!QDir(d).entryList({QStringLiteral("*.traineddata")}, QDir::Files).isEmpty())
            return QDir(d).absolutePath();
    return {};
}

QStringList ocrAvailableLanguages()
{
    const QString dir = tessdataDir();
    if (dir.isEmpty())
        return {};
    QStringList out;
    for (const QFileInfo &fi : QDir(dir).entryInfoList({QStringLiteral("*.traineddata")}, QDir::Files, QDir::Name)) {
        const QString code = fi.completeBaseName();
        if (code != QLatin1String("osd") && code != QLatin1String("equ"))
            out << code;
    }
    return out;
}

QString ocrLanguageName(const QString &code)
{
    static const QHash<QString, QLocale::Language> map = {
        {QStringLiteral("eng"), QLocale::English},   {QStringLiteral("fra"), QLocale::French},
        {QStringLiteral("deu"), QLocale::German},    {QStringLiteral("spa"), QLocale::Spanish},
        {QStringLiteral("ita"), QLocale::Italian},   {QStringLiteral("por"), QLocale::Portuguese},
        {QStringLiteral("nld"), QLocale::Dutch},     {QStringLiteral("pol"), QLocale::Polish},
        {QStringLiteral("rus"), QLocale::Russian},   {QStringLiteral("ukr"), QLocale::Ukrainian},
        {QStringLiteral("ara"), QLocale::Arabic},    {QStringLiteral("tur"), QLocale::Turkish},
        {QStringLiteral("jpn"), QLocale::Japanese},  {QStringLiteral("kor"), QLocale::Korean},
        {QStringLiteral("chi_sim"), QLocale::Chinese}, {QStringLiteral("chi_tra"), QLocale::Chinese},
        {QStringLiteral("swe"), QLocale::Swedish},   {QStringLiteral("dan"), QLocale::Danish},
        {QStringLiteral("nor"), QLocale::NorwegianBokmal}, {QStringLiteral("fin"), QLocale::Finnish},
        {QStringLiteral("ces"), QLocale::Czech},     {QStringLiteral("ell"), QLocale::Greek},
        {QStringLiteral("hun"), QLocale::Hungarian}, {QStringLiteral("ron"), QLocale::Romanian},
        {QStringLiteral("cat"), QLocale::Catalan},   {QStringLiteral("heb"), QLocale::Hebrew},
    };
    const auto it = map.constFind(code);
    if (it == map.constEnd())
        return code;
    QString name = QLocale::languageToString(*it);
    if (code == QLatin1String("chi_sim"))
        name += QCoreApplication::translate("Ocr", " (simplified)");
    else if (code == QLatin1String("chi_tra"))
        name += QCoreApplication::translate("Ocr", " (traditional)");
    return name;
}

QString ocrDefaultLanguages()
{
    const QStringList avail = ocrAvailableLanguages();
    QStringList langs;
    static const QHash<QString, QString> iso = {
        {QStringLiteral("fr"), QStringLiteral("fra")}, {QStringLiteral("de"), QStringLiteral("deu")},
        {QStringLiteral("es"), QStringLiteral("spa")}, {QStringLiteral("it"), QStringLiteral("ita")},
        {QStringLiteral("pt"), QStringLiteral("por")}, {QStringLiteral("nl"), QStringLiteral("nld")},
        {QStringLiteral("pl"), QStringLiteral("pol")}, {QStringLiteral("ru"), QStringLiteral("rus")},
        {QStringLiteral("ja"), QStringLiteral("jpn")}, {QStringLiteral("zh"), QStringLiteral("chi_sim")},
    };
    const QString sys = iso.value(QLocale().name().section(QLatin1Char('_'), 0, 0));
    if (!sys.isEmpty() && avail.contains(sys))
        langs << sys;
    if (avail.contains(QStringLiteral("eng")))
        langs << QStringLiteral("eng");
    if (langs.isEmpty() && !avail.isEmpty())
        langs << avail.first();
    return langs.join(QLatin1Char('+'));
}

bool ocrToPdf(const QStringList &files, const QString &outPath, const QString &languages, const QString &title,
              const ExportProgress &progress, QString *error)
{
    QString dummy;
    QString *err = error ? error : &dummy;
    Engine engine;
    if (!engine.init(languages, err))
        return false;
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        *err = QCoreApplication::translate("Ocr", "Cannot create a temporary folder");
        return false;
    }
    // The renderer appends ".pdf"; write to a temporary name, then move into place.
    const QString base = tmp.filePath(QStringLiteral("out"));
    tesseract::TessPDFRenderer renderer(QFile::encodeName(base).constData(),
                                        QFile::encodeName(engine.dir()).constData(), false);
    if (!renderer.BeginDocument(title.toUtf8().constData())) {
        *err = QCoreApplication::translate("Ocr", "Cannot write %1").arg(outPath);
        return false;
    }
    int done = 0;
    for (const QString &file : files) {
        bool rotated = false;
        PixPtr pix = engine.load(file, err, &rotated);
        if (!pix)
            return false;
        // The renderer embeds the input file as is when it is a JPEG; give it the
        // upright page when OSD rotated it.
        QString input = file;
        if (rotated) {
            input = tmp.filePath(QStringLiteral("page%1.jpg").arg(done));
            pixWriteJpeg(QFile::encodeName(input).constData(), pix.get(), 90, 0);
        }
        auto &api = engine.api();
        api.SetInputName(QFile::encodeName(input).constData());
        api.SetImage(pix.get());
        api.SetSourceResolution(estimateDpi(pix.get()));
        if (api.Recognize(nullptr) != 0 || !renderer.AddImage(&api)) {
            *err = QCoreApplication::translate("Ocr", "Text recognition failed on %1").arg(file);
            return false;
        }
        api.Clear();
        ++done;
        if (progress && !progress(done)) {
            *err = QCoreApplication::translate("Ocr", "Cancelled");
            return false;
        }
    }
    if (!renderer.EndDocument()) {
        *err = QCoreApplication::translate("Ocr", "Cannot write %1").arg(outPath);
        return false;
    }
    QFile::remove(outPath);
    if (!QFile::copy(base + QStringLiteral(".pdf"), outPath)) {
        *err = QCoreApplication::translate("Ocr", "Cannot write %1").arg(outPath);
        return false;
    }
    return true;
}

bool ocrToText(const QStringList &files, const QString &outPath, const QString &languages,
               const ExportProgress &progress, QString *error)
{
    QString dummy;
    QString *err = error ? error : &dummy;
    Engine engine;
    if (!engine.init(languages, err))
        return false;
    QString text;
    int done = 0;
    for (const QString &file : files) {
        PixPtr pix = engine.load(file, err);
        if (!pix)
            return false;
        auto &api = engine.api();
        api.SetImage(pix.get());
        api.SetSourceResolution(estimateDpi(pix.get()));
        if (done > 0)
            text += QLatin1Char('\f');
        text += utf8(api.GetUTF8Text());
        api.Clear();
        ++done;
        if (progress && !progress(done)) {
            *err = QCoreApplication::translate("Ocr", "Cancelled");
            return false;
        }
    }
    return writeFile(outPath, text.toUtf8(), err);
}

bool ocrToDocx(const QStringList &files, const QString &outPath, const QString &languages,
               const ExportProgress &progress, QString *error)
{
    QString dummy;
    QString *err = error ? error : &dummy;
    Engine engine;
    if (!engine.init(languages, err))
        return false;
    QList<QStringList> pages;
    int done = 0;
    for (const QString &file : files) {
        PixPtr pix = engine.load(file, err);
        if (!pix)
            return false;
        auto &api = engine.api();
        api.SetImage(pix.get());
        api.SetSourceResolution(estimateDpi(pix.get()));
        if (api.Recognize(nullptr) != 0) {
            *err = QCoreApplication::translate("Ocr", "Text recognition failed on %1").arg(file);
            return false;
        }
        pages << paragraphs(api);
        api.Clear();
        ++done;
        if (progress && !progress(done)) {
            *err = QCoreApplication::translate("Ocr", "Cancelled");
            return false;
        }
    }
    return writeFile(outPath, docxFromPages(pages), err);
}

QByteArray docxFromPages(const QList<QStringList> &pages)
{
    QString body;
    for (int p = 0; p < pages.size(); ++p) {
        for (const QString &para : pages[p])
            body += QStringLiteral("<w:p><w:r><w:t xml:space=\"preserve\">%1</w:t></w:r></w:p>").arg(xmlEscape(para));
        if (p + 1 < pages.size())
            body += QStringLiteral("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");
    }
    const QByteArray document =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>" +
        body.toUtf8() +
        "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
        "<w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" w:left=\"1134\" w:header=\"708\" "
        "w:footer=\"708\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>";
    const QByteArray contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>";
    const QByteArray rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"word/document.xml\"/></Relationships>";
    return storedZip({{QStringLiteral("[Content_Types].xml"), contentTypes},
                      {QStringLiteral("_rels/.rels"), rels},
                      {QStringLiteral("word/document.xml"), document}});
}

QByteArray storedZip(const QList<QPair<QString, QByteArray>> &entries)
{
    // Minimal ZIP writer: no compression ("stored"), which every reader accepts.
    QByteArray out, central;
    const quint16 dosTime = 0, dosDate = (2026 - 1980) << 9 | 1 << 5 | 1;
    for (const auto &e : entries) {
        const QByteArray name = e.first.toUtf8();
        const quint32 crc = quint32(crc32(0, reinterpret_cast<const Bytef *>(e.second.constData()),
                                          uInt(e.second.size())));
        const quint32 offset = quint32(out.size());
        put32(out, 0x04034b50);
        put16(out, 10);    // version needed
        put16(out, 0x800); // UTF-8 names
        put16(out, 0);     // stored
        put16(out, dosTime);
        put16(out, dosDate);
        put32(out, crc);
        put32(out, quint32(e.second.size()));
        put32(out, quint32(e.second.size()));
        put16(out, quint16(name.size()));
        put16(out, 0);
        out += name;
        out += e.second;

        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 10);
        put16(central, 0x800);
        put16(central, 0);
        put16(central, dosTime);
        put16(central, dosDate);
        put32(central, crc);
        put32(central, quint32(e.second.size()));
        put32(central, quint32(e.second.size()));
        put16(central, quint16(name.size()));
        put16(central, 0); // extra
        put16(central, 0); // comment
        put16(central, 0); // disk
        put16(central, 0); // internal attributes
        put32(central, 0); // external attributes
        put32(central, offset);
        central += name;
    }
    const quint32 centralOffset = quint32(out.size());
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(entries.size()));
    put16(out, quint16(entries.size()));
    put32(out, quint32(central.size()));
    put32(out, centralOffset);
    put16(out, 0);
    return out;
}

namespace {

// x of the vertical ruling lines of a table (lines at least 4% of the page high).
QVector<int> verticalRulings(Pix *pix)
{
    PixPtr gray(pixConvertTo8(pix, 0));
    if (!gray)
        return {};
    const int w = pixGetWidth(gray.get()), h = pixGetHeight(gray.get());
    cv::Mat m(h, w, CV_8UC1);
    const l_uint32 *data = pixGetData(gray.get());
    const int wpl = pixGetWpl(gray.get());
    for (int y = 0; y < h; ++y) {
        const l_uint32 *line = data + y * wpl;
        uchar *row = m.ptr<uchar>(y);
        for (int x = 0; x < w; ++x)
            row[x] = uchar(GET_DATA_BYTE(line, x));
    }
    cv::Mat bin;
    cv::adaptiveThreshold(m, bin, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY_INV, 25, 15);
    const int len = std::max(15, h / 25);
    cv::morphologyEx(bin, bin, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, len)));
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    QVector<int> xs;
    for (const auto &c : contours) {
        const cv::Rect r = cv::boundingRect(c);
        if (r.height >= len && r.width <= std::max(6, w / 150))
            xs << r.x + r.width / 2;
    }
    std::sort(xs.begin(), xs.end());
    QVector<int> merged; // one x per ruling line (a line can be broken in pieces)
    for (int x : std::as_const(xs))
        if (merged.isEmpty() || x - merged.last() > std::max(8, w / 100))
            merged << x;
    return merged.size() >= 2 ? merged : QVector<int>();
}

QString columnName(int c)
{
    QString n;
    for (++c; c > 0; c = (c - 1) / 26)
        n.prepend(QChar(u'A' + (c - 1) % 26));
    return n;
}

} // namespace

QVector<QStringList> tableFromWords(const QVector<OcrWord> &input, const QVector<int> &columnEdges)
{
    if (input.isEmpty())
        return {};
    QVector<OcrWord> words = input;
    std::stable_sort(words.begin(), words.end(), [](const OcrWord &a, const OcrWord &b) {
        return a.line != b.line ? a.line < b.line : a.box.left() < b.box.left();
    });
    QVector<int> heights;
    for (const OcrWord &w : std::as_const(words))
        heights << w.box.height();
    std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
    const int h = std::max(4, heights[heights.size() / 2]);

    // Segments: runs of words of one line with small gaps (a cell's content).
    struct Segment {
        int line, left, right;
        QString text;
    };
    QVector<Segment> segs;
    for (const OcrWord &w : std::as_const(words)) {
        const bool gapBreak = !columnEdges.isEmpty()
                                  ? false
                                  : (!segs.isEmpty() && segs.last().line == w.line && w.box.left() - segs.last().right > h * 6 / 5);
        bool rulingBreak = false;
        if (!columnEdges.isEmpty() && !segs.isEmpty() && segs.last().line == w.line)
            for (int x : columnEdges)
                if (x > segs.last().right - h / 4 && x < w.box.left() + h / 4)
                    rulingBreak = true;
        if (segs.isEmpty() || segs.last().line != w.line || gapBreak || rulingBreak) {
            segs.append({w.line, w.box.left(), w.box.right(), w.text});
        } else {
            segs.last().right = std::max(segs.last().right, w.box.right());
            segs.last().text += QLatin1Char(' ') + w.text;
        }
    }

    // Column starts: the ruling lines, or left edges shared by several multi-cell lines.
    QVector<int> starts;
    if (!columnEdges.isEmpty()) {
        starts << std::numeric_limits<int>::min();
        for (int x : columnEdges)
            starts << x;
    } else {
        QHash<int, int> segsPerLine;
        for (const Segment &s : std::as_const(segs))
            ++segsPerLine[s.line];
        QVector<int> lefts;
        int multiLines = 0;
        for (auto it = segsPerLine.cbegin(); it != segsPerLine.cend(); ++it)
            multiLines += it.value() >= 2;
        for (const Segment &s : std::as_const(segs))
            if (segsPerLine.value(s.line) >= 2)
                lefts << s.left;
        std::sort(lefts.begin(), lefts.end());
        const int needed = std::max(2, multiLines * 3 / 10);
        for (int i = 0; i < lefts.size();) {
            int j = i;
            while (j + 1 < lefts.size() && lefts[j + 1] - lefts[i] <= 2 * h)
                ++j;
            if (j - i + 1 >= needed)
                starts << lefts[i] - h;
            i = j + 1;
        }
        if (starts.isEmpty() || starts.first() > segs.first().left)
            starts.prepend(std::numeric_limits<int>::min());
        starts.first() = std::numeric_limits<int>::min();
    }

    QVector<QStringList> rows;
    int currentLine = std::numeric_limits<int>::min();
    for (const Segment &s : std::as_const(segs)) {
        if (s.line != currentLine) {
            rows.append(QStringList());
            currentLine = s.line;
        }
        QStringList &row = rows.last();
        int col = 0;
        while (col + 1 < starts.size() && starts[col + 1] <= s.left + h / 2)
            ++col;
        while (col < row.size() && !row[col].isEmpty())
            ++col; // right-aligned text that starts left of its column
        while (row.size() <= col)
            row << QString();
        row[col] = s.text;
    }
    return rows;
}

QByteArray xlsxFromSheets(const QList<QPair<QString, QVector<QStringList>>> &sheets)
{
    QList<QPair<QString, QByteArray>> parts;
    QByteArray workbookSheets, workbookRels, overrides;
    for (int i = 0; i < sheets.size(); ++i) {
        const QByteArray n = QByteArray::number(i + 1);
        QString data;
        const auto &rows = sheets[i].second;
        for (int r = 0; r < rows.size(); ++r) {
            data += QStringLiteral("<row r=\"%1\">").arg(r + 1);
            for (int c = 0; c < rows[r].size(); ++c)
                if (!rows[r][c].isEmpty())
                    data += QStringLiteral("<c r=\"%1%2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%3</t></is></c>")
                                .arg(columnName(c))
                                .arg(r + 1)
                                .arg(xmlEscape(rows[r][c]));
            data += QStringLiteral("</row>");
        }
        parts << qMakePair(QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1),
                           "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                           "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>" +
                               data.toUtf8() + "</sheetData></worksheet>");
        // Sheet names: at most 31 characters, none of []:*?/\ .
        QString name = sheets[i].first;
        name.remove(QRegularExpression(QStringLiteral("[\\[\\]:*?/\\\\]")));
        name = name.left(31);
        if (name.isEmpty())
            name = QStringLiteral("Sheet%1").arg(i + 1);
        workbookSheets += "<sheet name=\"" + xmlEscape(name).toUtf8() + "\" sheetId=\"" + n + "\" r:id=\"rId" + n + "\"/>";
        workbookRels += "<Relationship Id=\"rId" + n +
                        "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                        "Target=\"worksheets/sheet" + n + ".xml\"/>";
        overrides += "<Override PartName=\"/xl/worksheets/sheet" + n +
                     ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
    }
    const QByteArray contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>" +
        overrides + "</Types>";
    const QByteArray rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"xl/workbook.xml\"/></Relationships>";
    const QByteArray workbook =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>" +
        workbookSheets + "</sheets></workbook>";
    const QByteArray workbookRelsXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">" +
        workbookRels + "</Relationships>";
    parts.prepend(qMakePair(QStringLiteral("xl/_rels/workbook.xml.rels"), workbookRelsXml));
    parts.prepend(qMakePair(QStringLiteral("xl/workbook.xml"), workbook));
    parts.prepend(qMakePair(QStringLiteral("_rels/.rels"), rels));
    parts.prepend(qMakePair(QStringLiteral("[Content_Types].xml"), contentTypes));
    return storedZip(parts);
}

bool ocrToXlsx(const QStringList &files, const QString &outPath, const QString &languages,
               const ExportProgress &progress, QString *error)
{
    QString dummy;
    QString *err = error ? error : &dummy;
    Engine engine;
    if (!engine.init(languages, err))
        return false;
    QList<QPair<QString, QVector<QStringList>>> sheets;
    int done = 0;
    for (const QString &file : files) {
        PixPtr pix = engine.load(file, err);
        if (!pix)
            return false;
        auto &api = engine.api();
        api.SetImage(pix.get());
        api.SetSourceResolution(estimateDpi(pix.get()));
        if (api.Recognize(nullptr) != 0) {
            *err = QCoreApplication::translate("Ocr", "Text recognition failed on %1").arg(file);
            return false;
        }
        QVector<OcrWord> words;
        std::unique_ptr<tesseract::ResultIterator> it(api.GetIterator());
        int line = -1;
        if (it) {
            do {
                if (it->Empty(tesseract::RIL_WORD))
                    continue;
                if (it->IsAtBeginningOf(tesseract::RIL_TEXTLINE))
                    ++line;
                int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                it->BoundingBox(tesseract::RIL_WORD, &x1, &y1, &x2, &y2);
                const QString text = utf8(it->GetUTF8Text(tesseract::RIL_WORD)).trimmed();
                if (!text.isEmpty())
                    words.append({QRect(QPoint(x1, y1), QPoint(x2, y2)), text, std::max(line, 0)});
            } while (it->Next(tesseract::RIL_WORD));
        }
        sheets << qMakePair(QCoreApplication::translate("Ocr", "Page %1").arg(done + 1),
                            tableFromWords(words, verticalRulings(pix.get())));
        api.Clear();
        ++done;
        if (progress && !progress(done)) {
            *err = QCoreApplication::translate("Ocr", "Cancelled");
            return false;
        }
    }
    return writeFile(outPath, xlsxFromSheets(sheets), err);
}

} // namespace lensscan
