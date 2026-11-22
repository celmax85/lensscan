// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests without a camera: protocol helpers, JPEG handling, page detection,
// color modes and exports on synthetic images.
#include "device/SonixAsic.h"
#include "device/UvcDescriptors.h"
#include "export/Exporters.h"
#include "imaging/AutoScan.h"
#include "imaging/BatchTools.h"
#include "imaging/Barcode.h"
#include "imaging/DocumentTools.h"
#include "media/Recorder.h"
#include "imaging/BookProcessor.h"
#include "imaging/ColorModes.h"
#include "imaging/Jpeg.h"
#include "imaging/PageDetector.h"
#include "imaging/ScanProcessor.h"
#include "ocr/Ocr.h"

#include <QGuiApplication>
#include <QStandardPaths>
#include <QProcess>
#include <QThread>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QTemporaryDir>

#include <cmath>
#include <string>

#include <opencv2/imgproc.hpp>
#include <cstdio>

using namespace lensscan;

static int g_failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

// A white sheet with black "text" lines, rotated 8 degrees, on a brown desk.
static QImage syntheticPage(QPolygonF *corners)
{
    QImage img(1600, 1200, QImage::Format_RGB888);
    img.fill(QColor(0x6b, 0x55, 0x48));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QTransform t;
    t.translate(800, 600);
    t.rotate(8);
    const QRectF sheet(-360, -480, 720, 960);
    p.setTransform(t);
    p.fillRect(sheet, QColor(0xf2, 0xf0, 0xea));
    p.setPen(QPen(Qt::black, 6));
    for (int y = -420; y < 420; y += 60)
        p.drawLine(QPointF(-300, y), QPointF(260, y));
    p.end();
    if (corners)
        *corners = t.map(QPolygonF(sheet));
    return img;
}

static void testProtocol()
{
    Guid g{};
    CHECK(guidFromString("28f03370-6311-4a2e-ba2c-6890eb334016", g));
    CHECK(guidToString(g) == "{28f03370-6311-4a2e-ba2c-6890eb334016}");
    CHECK(g[0] == 0x70 && g[3] == 0x28); // first group little-endian on the wire
    const auto rd = sonixReadRequest(0x1005);
    CHECK(rd[0] == 0x05 && rd[1] == 0x10 && rd[2] == 0x00 && rd[3] == 0xff);
    const auto wr = sonixWriteRequest(0x1005, 0x5a);
    CHECK(wr[0] == 0x05 && wr[1] == 0x10 && wr[2] == 0x5a && wr[3] == 0x00);
}

static void testJpeg()
{
    QPolygonF corners;
    const QImage img = syntheticPage(&corners);
    const QByteArray jpg = encodeJpeg(img, 90);
    CHECK(jpg.size() > 1024);
    CHECK(looksLikeJpeg(reinterpret_cast<const uint8_t *>(jpg.constData()), size_t(jpg.size())));
    CHECK(jpegSize(jpg) == img.size());
    CHECK(decodeJpeg(jpg).size() == img.size());
    const QImage small = decodeJpeg(jpg, 400);
    CHECK(small.width() == 400 && small.height() == 300); // 1/4 DCT scaling
    QByteArray broken = jpg.left(jpg.size() / 2);
    CHECK(!looksLikeJpeg(reinterpret_cast<const uint8_t *>(broken.constData()), size_t(broken.size())));
    CHECK(decodeJpeg(broken).isNull());
    QByteArray garbage(20000, '\x28');
    CHECK(decodeJpeg(garbage).isNull());
}

static void testPageDetector()
{
    QPolygonF truth;
    const QImage img = syntheticPage(&truth);
    const QPolygonF q = PageDetector::detect(img);
    CHECK(q.size() == 4);
    if (q.size() == 4) {
        // Every detected corner must be close to a true corner.
        for (const QPointF &c : q) {
            double best = 1e9;
            for (const QPointF &t : truth)
                best = std::min(best, std::hypot(c.x() - t.x(), c.y() - t.y()));
            CHECK(best < 25);
        }
        const QImage flat = PageDetector::warp(img, q);
        CHECK(std::abs(flat.width() - 720) < 40 && std::abs(flat.height() - 960) < 40);
    }
    QImage empty(800, 600, QImage::Format_RGB888);
    empty.fill(QColor(0x6b, 0x55, 0x48));
    CHECK(PageDetector::detect(empty).isEmpty());
}

static void testColorModes()
{
    const QImage img = syntheticPage(nullptr);
    CHECK(applyColorMode(img, ColorMode::Gray).format() == QImage::Format_Grayscale8);
    const QImage bw = applyColorMode(img, ColorMode::BlackWhite);
    CHECK(bw.format() == QImage::Format_Grayscale8 && isBinaryGray(bw));
    for (ColorMode m : {ColorMode::Auto, ColorMode::Color, ColorMode::WhitePaperStamp, ColorMode::Watermark})
        CHECK(applyColorMode(img, m).size() == img.size());
    CHECK(colorModeFromKey(colorModeKey(ColorMode::WhitePaperStamp)) == ColorMode::WhitePaperStamp);
    CHECK(rotateImage(img, 90).size() == QSize(1200, 1600));
    CHECK(rotateImage(img, -90).size() == QSize(1200, 1600));
}

static void testPipelineAndExport()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QByteArray jpg = encodeJpeg(syntheticPage(nullptr), 90);

    ScanOptions opt;
    opt.outputDir = dir.path();
    opt.colorMode = ColorMode::None;
    opt.pageMode = PageMode::Original;
    const ScanResult raw = processAndSave(jpg, opt);
    CHECK(raw.error.isEmpty());
    QFile f(raw.path);
    // Untouched: the camera's JPEG data is kept, only its resolution field changes.
    CHECK(f.open(QIODevice::ReadOnly) && f.readAll().endsWith(jpg.mid(jpg.indexOf("\xff\xdb"))));
    CHECK(imageDpi(loadScan(raw.path)) == 300);

    opt.pageMode = PageMode::FlatPage;
    opt.colorMode = ColorMode::Auto;
    const ScanResult flat = processAndSave(jpg, opt);
    CHECK(flat.error.isEmpty() && QFileInfo(flat.path).suffix() == QLatin1String("jpg"));
    CHECK(std::abs(loadScan(flat.path).height() - 960) < 40);

    opt.colorMode = ColorMode::BlackWhite;
    const ScanResult bw = processAndSave(jpg, opt);
    CHECK(bw.error.isEmpty() && QFileInfo(bw.path).suffix() == QLatin1String("png"));

    opt.pageMode = PageMode::Area;
    opt.colorMode = ColorMode::Color;
    opt.area = QRectF(0.25, 0.25, 0.5, 0.5);
    const ScanResult area = processAndSave(jpg, opt);
    CHECK(loadScan(area.path).size() == QSize(800, 600));

    const QStringList files = {raw.path, flat.path, bw.path};
    QString err;
    const QString pdf = dir.filePath(QStringLiteral("out.pdf"));
    CHECK(exportPdf(files, pdf, PdfOptions(), nullptr, &err));
    QFile pf(pdf);
    CHECK(pf.open(QIODevice::ReadOnly) && pf.read(5) == "%PDF-");
    CHECK(pf.size() > 10000);
    const QString tif = dir.filePath(QStringLiteral("out.tif"));
    TiffOptions to;
    to.jpegQuality = 85;
    CHECK(exportTiff(files, tif, to, nullptr, &err));
    CHECK(QFileInfo(tif).size() > 10000);
    // One file per page, lossless.
    to.multiPage = false;
    to.compression = TiffOptions::Compression::Lzw;
    const QString each = dir.filePath(QStringLiteral("each.tif"));
    CHECK(exportTiff(files, each, to, nullptr, &err));
    for (const QString &p : tiffOutputPaths(each, int(files.size()), false))
        CHECK(QFileInfo(p).size() > 1000);
    to.compression = TiffOptions::Compression::Group4;
    to.multiPage = true;
    CHECK(exportTiff(files, dir.filePath(QStringLiteral("g4.tif")), to, nullptr, &err));
    CHECK(exportImages(files, dir.filePath(QStringLiteral("png")), QStringLiteral("png"), 90, nullptr, &err));
    CHECK(QFileInfo::exists(dir.filePath(QStringLiteral("png/")) + QFileInfo(raw.path).completeBaseName() +
                            QStringLiteral(".png")));
}

// A flat page of text lines for the book test.
static cv::Mat bookPage(int seed)
{
    cv::Mat p(1400, 1000, CV_8UC3, cv::Scalar(245, 245, 245));
    const char *words[] = {"the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog", "books", "camera"};
    unsigned r = unsigned(seed);
    for (int y = 120; y < 1300; y += 58) {
        std::string line;
        for (int w = 0; w < 7; ++w) {
            r = r * 1103515245u + 12345u;
            line += std::string(words[(r >> 16) % 10]) + ' ';
        }
        cv::putText(p, line, {80, y}, cv::FONT_HERSHEY_SIMPLEX, 1.25, cv::Scalar(20, 20, 20), 2, cv::LINE_AA);
    }
    return p;
}

// An open book seen by a camera 1.8 page heights above it: each page bends up from the
// spine (tangent angle 55 degrees at the spine, flat at the outer edge), rendered strip
// by strip with a pinhole projection. A skin-coloured "finger" holds the right page.
static QImage syntheticBook()
{
    const int W = 1000, H = 1400, IW = 3264, IH = 2448;
    cv::Mat img(IH, IW, CV_8UC3, cv::Scalar(70, 45, 60));
    const double D = 1.8 * H, f = 0.42 * IW * D / W, cx = IW / 2.0, cy = IH / 2.0, th0 = 55 * M_PI / 180;
    const int n = 300;
    std::vector<double> X(n, 0), Z(n, 0);
    for (int i = 1; i < n; ++i) {
        const double s = W * double(i - 1) / (n - 1), th = th0 * std::pow(1 - s / W, 2), ds = double(W) / (n - 1);
        X[size_t(i)] = X[size_t(i - 1)] + std::cos(th) * ds;
        Z[size_t(i)] = Z[size_t(i - 1)] + std::sin(th) * ds;
    }
    for (int side : {-1, 1}) {
        const cv::Mat src = bookPage(side < 0 ? 1 : 2);
        for (int i = 0; i + 1 < n; ++i) {
            const double u0 = double(W) * i / (n - 1), u1 = double(W) * (i + 1) / (n - 1);
            auto proj = [&](int k, double Y) {
                return cv::Point2f(float(cx + f * side * X[size_t(k)] / (D - Z[size_t(k)])),
                                   float(cy + f * Y / (D - Z[size_t(k)])));
            };
            const float sa = float(side > 0 ? u0 : W - u0), sb = float(side > 0 ? u1 : W - u1);
            const cv::Point2f s4[4] = {{sa, 0}, {sa, float(H)}, {sb, 0}, {sb, float(H)}};
            const cv::Point2f d4[4] = {proj(i, -H / 2.0), proj(i, H / 2.0), proj(i + 1, -H / 2.0),
                                       proj(i + 1, H / 2.0)};
            const cv::Mat M = cv::getPerspectiveTransform(s4, d4);
            cv::Mat warped, mask(H, W, CV_8U, cv::Scalar(255)), wmask;
            cv::warpPerspective(src, warped, M, img.size());
            cv::warpPerspective(mask, wmask, M, img.size(), cv::INTER_NEAREST);
            std::vector<cv::Point> quad = {cv::Point(d4[0]), cv::Point(d4[2]), cv::Point(d4[3]), cv::Point(d4[1])};
            cv::Mat clip = cv::Mat::zeros(img.size(), CV_8U);
            cv::fillConvexPoly(clip, quad, cv::Scalar(255));
            warped.copyTo(img, wmask & clip);
        }
    }
    cv::ellipse(img, cv::Point(int(cx + f * X.back() / D * 0.97), int(cy - 150)), cv::Size(110, 45), 0, 0, 360,
                cv::Scalar(215, 170, 140), -1);
    const cv::Mat rot = cv::getRotationMatrix2D(cv::Point2f(float(cx), float(cy)), 4, 1);
    cv::warpAffine(img, img, rot, img.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(70, 45, 60));
    return matToImage(img);
}

// Vertical offset between the text lines of the left and right thirds of a page:
// zero when lines are straight.
static int lineOffset(const QImage &page)
{
    cv::Mat g;
    cv::cvtColor(matView(page), g, cv::COLOR_RGB2GRAY);
    const int w = g.cols;
    auto profile = [&](int x0, int x1) {
        std::vector<double> p(size_t(g.rows), 0);
        for (int y = 0; y < g.rows; ++y)
            for (int x = x0; x < x1; ++x)
                p[size_t(y)] += g.at<uchar>(y, x) < 128;
        return p;
    };
    const auto a = profile(w / 10, w / 3), b = profile(2 * w / 3, 9 * w / 10);
    int best = 0;
    double bestScore = -1;
    for (int d = -40; d <= 40; ++d) {
        double sc = 0;
        for (int y = 40; y + 40 < g.rows; ++y)
            sc += a[size_t(y)] * b[size_t(y + d)];
        if (sc > bestScore) {
            bestScore = sc;
            best = d;
        }
    }
    return best;
}

static void testBook()
{
    const QImage book = syntheticBook();
    const BookGeometry geo = detectBook(book);
    CHECK(geo.valid);
    CHECK(std::abs(geo.angle + 4) < 1.5); // the book was rotated 4 degrees
    const QVector<QImage> pages = processBook(book, {true, 0.42 * 3264 * 1.8 * 1400 / 1000});
    CHECK(pages.size() == 2);
    if (pages.size() != 2)
        return;
    for (const QImage &p : pages) {
        const double aspect = double(p.width()) / p.height();
        CHECK(std::abs(aspect - 1000.0 / 1400) < 0.05);
        const int off = lineOffset(p);
        if (std::abs(off) > 3)
            std::printf("book page %dx%d: text line offset %d\n", p.width(), p.height(), off);
        CHECK(std::abs(off) <= 3);
    }
    // A flat sheet, even tilted and in perspective, is not mistaken for a book.
    {
        cv::Mat desk(2448, 3264, CV_8UC3, cv::Scalar(70, 45, 60));
        const std::vector<cv::Point> sheet = {{700, 400}, {2700, 300}, {2800, 2100}, {600, 2200}};
        cv::fillConvexPoly(desk, sheet, cv::Scalar(245, 245, 245));
        cv::rectangle(desk, cv::Rect(900, 1300, 1200, 600), cv::Scalar(200, 40, 30), -1);
        CHECK(!detectBook(matToImage(desk)).valid);
    }
    // The finger on the right page has been painted over.
    QImage right = pages[1];
    CHECK(removeFingers(right, false) == 0);
}

// Simulated preview: a page, a hand passing over it, the next page, sensor noise.
static void testAutoScan()
{
    auto page = [](int seed) {
        cv::Mat m = bookPage(seed);
        cv::Mat frame(960, 1280, CV_8UC3, cv::Scalar(60, 50, 50));
        cv::Mat small;
        cv::resize(m, small, cv::Size(640, 896));
        small.copyTo(frame(cv::Rect(320, 32, 640, 896)));
        return frame;
    };
    unsigned r = 7;
    auto noisy = [&](const cv::Mat &m) {
        cv::Mat n(m.size(), CV_8UC3), out;
        cv::randn(n, 0, 3 + (r++ % 2));
        cv::add(m, n, out, cv::noArray(), CV_8UC3);
        return matToImage(out);
    };
    auto hand = [](const cv::Mat &m, int x) {
        cv::Mat h = m.clone();
        cv::ellipse(h, cv::Point(x, 480), cv::Size(260, 160), 0, 0, 360, cv::Scalar(215, 170, 140), -1);
        return h;
    };
    const cv::Mat a = page(1), b = page(2);
    AutoScanTrigger t;
    int64_t ms = 0;
    int fired = 0;
    auto run = [&](const cv::Mat &m, int frames) {
        for (int i = 0; i < frames; ++i, ms += 33)
            if (t.feed(noisy(m), ms)) {
                ++fired;
                t.scanned();
            }
    };
    run(a, 60);                       // page already there when auto scan starts: no scan
    CHECK(fired == 0);
    for (int x = 1300; x > 0; x -= 60) // the hand turns the page
        run(hand(x > 640 ? a : b, x), 1);
    run(b, 20);                       // hand gone, page settling: not yet
    CHECK(fired == 0);
    run(b, 40);
    CHECK(fired == 1);
    run(b, 90); // stays: no second scan
    CHECK(fired == 1);
    for (int x = 1300; x > 0; x -= 60) // a hand passes but the page is the same
        run(hand(b, x), 1);
    run(b, 60);
    CHECK(fired == 1);
}

static void testBatchTools()
{
    // Blank page: paper with uneven light, a shadow at the edge and dust specks.
    cv::Mat blank(2200, 1700, CV_8UC3);
    for (int y = 0; y < blank.rows; ++y)
        blank.row(y).setTo(cv::Scalar(225 + 20 * y / blank.rows, 228 + 20 * y / blank.rows, 230));
    cv::rectangle(blank, cv::Rect(0, 0, 40, 2200), cv::Scalar(90, 90, 90), -1);
    cv::Mat noise(blank.size(), CV_8UC3);
    cv::randn(noise, 0, 4);
    blank += noise;
    for (int i = 0; i < 6; ++i)
        cv::circle(blank, cv::Point(300 + 200 * i, 500 + 150 * i), 2, cv::Scalar(60, 60, 60), -1);
    const QImage blankImg = matToImage(blank);
    CHECK(isBlankPage(blankImg));
    // The same page with a single short line of text is not blank.
    cv::Mat line = blank.clone();
    cv::putText(line, "Page 12 - intentionally short", {400, 1100}, cv::FONT_HERSHEY_SIMPLEX, 1.4,
                cv::Scalar(30, 30, 30), 3, cv::LINE_AA);
    CHECK(!isBlankPage(matToImage(line)));
    CHECK(!isBlankPage(matToImage(bookPage(3))));

    // Adjustments.
    QImage mid(100, 100, QImage::Format_RGB888);
    mid.fill(QColor(100, 100, 100));
    CHECK(qGray(adjustImage(mid, {50, 0, 0}).pixel(50, 50)) > 150);
    CHECK(qGray(adjustImage(mid, {0, 60, 0}).pixel(50, 50)) < 100); // darker than mid-grey moves down
    CHECK(adjustImage(mid, {}).constBits() == mid.constBits());       // no-op keeps the image

    // Watermark draws something and keeps the size.
    QImage page(800, 1000, QImage::Format_RGB888);
    page.fill(Qt::white);
    Watermark wm;
    wm.text = QStringLiteral("CONFIDENTIEL");
    const QImage marked = addWatermark(page, wm);
    CHECK(marked.size() == page.size());
    int changed = 0;
    for (int y = 0; y < marked.height(); y += 4)
        for (int x = 0; x < marked.width(); x += 4)
            changed += marked.pixel(x, y) != page.pixel(x, y);
    CHECK(changed > 200);
    CHECK(trimEdges(page, 10).size() == QSize(780, 980));
    CHECK(cropNormalized(page, QRectF(0.25, 0.5, 0.5, 0.5)).size() == QSize(400, 500));

    // Image watermark.
    QImage logo(100, 40, QImage::Format_RGB888);
    logo.fill(Qt::blue);
    Watermark iw;
    iw.image = logo;
    iw.opacity = 100;
    iw.position = WatermarkPosition::Center;
    const QImage withLogo = addWatermark(page, iw);
    CHECK(withLogo.pixelColor(400, 500).blue() > 200 && withLogo.pixelColor(400, 500).red() < 50);
    CHECK(withLogo.pixelColor(20, 20) == QColor(Qt::white));

    // Overlapping edge cropping keeps the spine side.
    QImage spread(1000, 100, QImage::Format_RGB888);
    spread.fill(Qt::white);
    for (int y = 0; y < 100; ++y)
        spread.setPixelColor(999, y, Qt::black); // spine at the right of a left page
    CHECK(cropToPageWidth(spread, 800, true).width() == 800);
    CHECK(cropToPageWidth(spread, 800, true).pixelColor(799, 50) == QColor(Qt::black));
    CHECK(cropToPageWidth(spread, 800, false).pixelColor(799, 50) == QColor(Qt::white));

    // Several documents on a dark desk: a white receipt, a coloured card, a photo.
    {
        cv::Mat desk(2448, 3264, CV_8UC3, cv::Scalar(55, 50, 60));
        cv::Mat dn(desk.size(), CV_8UC3);
        cv::randn(dn, 0, 5);
        desk += dn;
        auto rotRect = [&](cv::Point2f c, cv::Size2f sz, float ang, cv::Scalar col) {
            cv::Point2f pts[4];
            cv::RotatedRect(c, sz, ang).points(pts);
            std::vector<cv::Point> poly(pts, pts + 4);
            cv::fillConvexPoly(desk, poly, col, cv::LINE_AA);
        };
        rotRect({700, 900}, {600, 1400}, 8, cv::Scalar(240, 240, 235));   // receipt
        rotRect({1900, 700}, {860, 540}, -12, cv::Scalar(40, 120, 200));  // card
        rotRect({2400, 1800}, {1000, 700}, 5, cv::Scalar(200, 160, 90));  // photo
        const QImage img = matToImage(desk);
        const QVector<QPolygonF> docs = detectDocuments(img);
        CHECK(docs.size() == 3);
        const QVector<QImage> crops = splitDocuments(img);
        CHECK(crops.size() == 3);
        if (crops.size() == 3) {
            const double r0 = double(std::max(crops[0].width(), crops[0].height())) /
                              std::min(crops[0].width(), crops[0].height());
            CHECK(std::abs(r0 - 1400.0 / 600) < 0.15); // the receipt, straightened
        }
    }

    // Rename with swapped names, then import.
    QTemporaryDir dir;
    const QString a = dir.filePath(QStringLiteral("b.jpg")), b = dir.filePath(QStringLiteral("a.jpg"));
    QImage small(20, 20, QImage::Format_RGB888);
    small.fill(Qt::red);
    small.save(a);
    small.fill(Qt::blue);
    small.save(b);
    // Ordered a then b -> "a" (red) becomes p01, "b" (blue) p02; then swap them back.
    const QStringList targets = renamedPaths({a, b}, QStringLiteral("p"), 1, 2);
    CHECK(QFileInfo(targets[0]).fileName() == QLatin1String("p01.jpg"));
    CHECK(renameFiles({a, b}, targets).isEmpty());
    CHECK(QImage(targets[0]).pixelColor(10, 10).red() > 200);
    CHECK(renameFiles({targets[0], targets[1]}, {targets[1], targets[0]}).isEmpty()); // swap
    CHECK(QImage(targets[0]).pixelColor(10, 10).blue() > 200);
    CHECK(!renameFiles({targets[0]}, {targets[1]}).isEmpty()); // would overwrite another page
    QTemporaryDir other;
    const QString tif = other.filePath(QStringLiteral("p01.ppm"));
    CHECK(small.save(tif, "PPM"));
    QStringList errors;
    const QStringList imported = importImages({targets[0], tif}, dir.path(), &errors);
    CHECK(errors.isEmpty() && imported.size() == 2);
    CHECK(QFileInfo(imported.value(0)).fileName() == QLatin1String("p01_2.jpg"));
    CHECK(QFileInfo(imported.value(1)).fileName() == QLatin1String("p01.png"));
}

// Renders a page of known text, rotated by `rotation` degrees, like a camera scan.
static QImage textPage(int rotation)
{
    QImage page(1700, 2200, QImage::Format_RGB888);
    page.fill(Qt::white);
    QPainter p(&page);
    QFont f(QStringLiteral("DejaVu Serif"));
    f.setPixelSize(46);
    p.setFont(f);
    p.setPen(Qt::black);
    const char *lines[] = {"LensScan recognizes printed text.", "The quick brown fox jumps over",
                           "the lazy dog near the river bank.", "", "Second paragraph: invoice 2026-118."};
    int y = 200;
    for (const char *l : lines) {
        p.drawText(150, y, QString::fromLatin1(l));
        y += 70;
    }
    p.end();
    return rotation ? rotateImage(page, rotation) : page;
}

static void testOcr()
{
    // storedZip: readable by a ZIP reader is checked through unzip in CI; here the layout.
    const QByteArray zip = storedZip({{QStringLiteral("a.txt"), QByteArray("hello")}});
    CHECK(zip.startsWith("PK\x03\x04") && zip.contains("hello") && zip.contains("PK\x05\x06"));
    const QByteArray docx = docxFromPages({{QStringLiteral("A & <b>")}, {QStringLiteral("Page 2")}});
    CHECK(docx.contains("A &amp; &lt;b&gt;") && docx.contains("w:type=\"page\""));

    if (ocrAvailableLanguages().isEmpty()) {
        std::printf("OCR: no Tesseract data, skipped\n");
        return;
    }
    QTemporaryDir dir;
    const QString upright = dir.filePath(QStringLiteral("p1.jpg")), sideways = dir.filePath(QStringLiteral("p2.jpg"));
    CHECK(saveImage(textPage(0), upright, 92));
    CHECK(saveImage(textPage(90), sideways, 92));
    QString err;
    const QString txt = dir.filePath(QStringLiteral("out.txt"));
    CHECK(ocrToText({upright, sideways}, txt, QStringLiteral("eng"), nullptr, &err));
    QFile tf(txt);
    CHECK(tf.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(tf.readAll());
    CHECK(text.contains(QLatin1String("quick brown fox")));
    if (text.count(QLatin1String("lazy dog")) != 2)
        std::printf("OCR text: [%s]\n", qPrintable(text));
    CHECK(text.count(QLatin1String("lazy dog")) == 2); // the sideways page was turned upright
    CHECK(text.contains(QLatin1Char('\f')));

    const QString pdf = dir.filePath(QStringLiteral("out.pdf"));
    CHECK(ocrToPdf({upright}, pdf, QStringLiteral("eng"), QStringLiteral("Test"), nullptr, &err));
    QFile pf(pdf);
    CHECK(pf.open(QIODevice::ReadOnly) && pf.readAll().startsWith("%PDF") && pf.size() > 20000);

    const QString dx = dir.filePath(QStringLiteral("out.docx"));
    CHECK(ocrToDocx({upright}, dx, QStringLiteral("eng"), nullptr, &err));
    QFile df(dx);
    CHECK(df.open(QIODevice::ReadOnly));
    const QByteArray d = df.readAll();
    CHECK(d.contains("quick brown fox jumps over the lazy dog")); // lines joined into one paragraph
    if (!err.isEmpty())
        std::printf("OCR error: %s\n", qPrintable(err));
}

// Needs the ffmpeg program: skipped when it is not installed (build containers).
static void testRecorder()
{
    if (Recorder::ffmpegPath().isEmpty()) {
        std::printf("recorder: ffmpeg not found, skipped\n");
        return;
    }
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("rec.mp4"));
    Recorder rec;
    QString err;
    const QString audio = qEnvironmentVariable("LENSSCAN_TEST_AUDIO"); // e.g. a pulse source name
    CHECK(rec.start(path, QSize(641, 481), 25, audio, &err));
    if (!rec.isRecording()) {
        std::printf("recorder: %s\n", qPrintable(err));
        return;
    }
    QImage frame(640, 480, QImage::Format_RGB32);
    int i = 0;
    const auto feed = [&](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            frame.fill(QColor::fromHsv((i * 7) % 360, 200, 220));
            rec.addFrame(frame);
            ++i;
            QThread::msleep(33);
        }
    };
    feed(1200);
    // A pause gives a second part, joined to the first one by stop().
    CHECK(rec.pause(&err) && rec.isPaused() && rec.isRecording());
    QThread::msleep(300);
    CHECK(rec.resume(&err) && !rec.isPaused());
    feed(1000);
    CHECK(rec.stop(&err));
    if (!err.isEmpty())
        std::printf("recorder: %s\n", qPrintable(err));
    CHECK(QFileInfo(path).size() > 1000);
    // Duration and streams, as ffprobe sees them.
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (!ffprobe.isEmpty()) {
        QProcess p;
        p.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                          QStringLiteral("format=duration:stream=codec_name,width,height"), QStringLiteral("-of"),
                          QStringLiteral("compact"), path});
        p.waitForFinished(10000);
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        std::printf("recorder: %s", qPrintable(out));
        CHECK(out.contains(QLatin1String("width=640")) && out.contains(QLatin1String("height=480")));
        const double dur = out.section(QLatin1String("duration="), 1).section(QLatin1Char('\n'), 0, 0).toDouble();
        CHECK(dur > 1.6 && dur < 2.6);
        if (!audio.isEmpty())
            CHECK(out.contains(QLatin1String("codec_name=aac")));
    }
}


static void testInsertionNames()
{
    auto sortedOk = [](QStringList all) {
        QStringList s = all;
        std::sort(s.begin(), s.end(), scanNameLess);
        return s == all;
    };
    const QStringList names = {QStringLiteral("Scan_0001.jpg"), QStringLiteral("Scan_0002.jpg"), QStringLiteral("Scan_0003.jpg")};
    const QStringList two = {QStringLiteral(".jpg"), QStringLiteral(".png")};
    for (int index = 0; index <= names.size(); ++index) {
        const QStringList n = insertionNames(names, index, two);
        CHECK(n.size() == 2);
        QStringList all = names;
        all.insert(index, n.value(1));
        all.insert(index, n.value(0));
        CHECK(sortedOk(all));
    }
    // A neighbour that is a prefix of the next name, and an empty folder.
    const QStringList tricky = {QStringLiteral("A.jpg"), QStringLiteral("A_001.jpg"), QStringLiteral("A_doc1.jpg")};
    const QStringList n = insertionNames(tricky, 2, {QStringLiteral(".jpg")});
    CHECK(n.size() == 1 && !tricky.contains(n.value(0)));
    QStringList all = tricky;
    all.insert(2, n.value(0));
    CHECK(sortedOk(all));
    CHECK(insertionNames({}, 0, {QStringLiteral(".jpg")}).size() == 1);
}

static void testHandwriting()
{
    QImage img(600, 400, QImage::Format_RGB888);
    img.fill(QColor(250, 250, 248));
    QPainter p(&img);
    p.fillRect(100, 100, 400, 6, Qt::black);  // printed line
    p.setPen(QPen(QColor(30, 60, 200), 5));    // blue pen crossing it
    p.drawLine(120, 60, 480, 160);
    p.end();
    const QImage out = eraseHandwriting(img, HandwritingOptions());
    CHECK(QColor(out.pixel(150, 68)).saturation() < 40); // the stroke is gone (away from the line)
    CHECK(QColor(out.pixel(150, 68)).value() > 200);
    CHECK(qGray(out.pixel(300, 103)) < 80);              // the print is kept, also at the crossing
    CHECK(qGray(out.pixel(110, 103)) < 80);
}

static void testPunchHoles()
{
    QImage img(1240, 1754, QImage::Format_RGB888);
    img.fill(QColor(245, 245, 240));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(25, 25, 25));
    p.drawEllipse(QPointF(70, 450), 30, 30);
    p.drawEllipse(QPointF(70, 1300), 30, 30);
    p.drawEllipse(QPointF(620, 900), 30, 30);   // a dot in the middle of the page: kept
    p.fillRect(300, 300, 600, 12, Qt::black);   // text-like bars: kept
    p.fillRect(300, 340, 500, 12, Qt::black);
    p.end();
    int n = 0;
    const QImage out = fillPunchHoles(img, 15, 60, &n);
    CHECK(n == 2);
    CHECK(qGray(out.pixel(70, 450)) > 200);
    CHECK(qGray(out.pixel(70, 1300)) > 200);
    CHECK(qGray(out.pixel(620, 900)) < 80);
    CHECK(qGray(out.pixel(500, 305)) < 80);
}

static void testPaperCorners()
{
    QImage img(800, 500, QImage::Format_RGB888);
    img.fill(QColor(30, 30, 30));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(240, 240, 235));
    p.drawRoundedRect(QRectF(0, 0, 800, 500), 45, 45);
    p.fillRect(200, 200, 300, 20, Qt::black);
    p.end();
    const QImage card = fillPaperCorners(img, false);
    CHECK(qGray(card.pixel(3, 3)) > 200);
    CHECK(qGray(card.pixel(796, 496)) > 200);
    CHECK(qGray(card.pixel(300, 210)) < 80);

    // A torn corner: a dark triangle well beyond the corner squares.
    QImage torn(800, 1000, QImage::Format_RGB888);
    torn.fill(QColor(240, 240, 235));
    QPainter q(&torn);
    q.setPen(Qt::NoPen);
    q.setBrush(QColor(30, 30, 30));
    q.drawPolygon(QPolygonF({QPointF(800, 0), QPointF(500, 0), QPointF(800, 300)}));
    q.fillRect(200, 500, 300, 20, Qt::black);
    q.end();
    const QImage fixedPage = fillPaperCorners(torn, true);
    CHECK(qGray(fixedPage.pixel(760, 60)) > 200);
    CHECK(qGray(fixedPage.pixel(300, 510)) < 80);
    CHECK(flipImage(torn, true).pixel(20, 20) == torn.pixel(779, 20));
}

static QImage ean13(const char *digits)
{
    static const char *L[] = {"0001101", "0011001", "0010011", "0111101", "0100011",
                              "0110001", "0101111", "0111011", "0110111", "0001011"};
    static const char *G[] = {"0100111", "0110011", "0011011", "0100001", "0011101",
                              "0111001", "0000101", "0010001", "0001001", "0010111"};
    static const char *R[] = {"1110010", "1100110", "1101100", "1000010", "1011100",
                              "1001110", "1010000", "1000100", "1001000", "1110100"};
    static const char *parity[] = {"LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG",
                                   "LGGLLG", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL"};
    std::string bits = "101";
    const char *par = parity[digits[0] - '0'];
    for (int i = 1; i <= 6; ++i)
        bits += (par[i - 1] == 'L' ? L : G)[digits[i] - '0'];
    bits += "01010";
    for (int i = 7; i <= 12; ++i)
        bits += R[digits[i] - '0'];
    bits += "101";
    const int module = 4, quiet = 60;
    QImage img(int(bits.size()) * module + 2 * quiet, 260, QImage::Format_RGB888);
    img.fill(Qt::white);
    QPainter p(&img);
    for (size_t i = 0; i < bits.size(); ++i)
        if (bits[i] == '1')
            p.fillRect(quiet + int(i) * module, 30, module, 200, Qt::black);
    return img;
}

static void testBarcode()
{
    QImage page(1200, 900, QImage::Format_RGB888);
    page.fill(QColor(245, 245, 245));
    QPainter p(&page);
    p.drawImage(300, 400, ean13("4006381333931"));
    p.end();
    const QVector<BarcodeResult> r = readBarcodes(page);
    CHECK(r.size() == 1);
    CHECK(r.value(0).type == QLatin1String("EAN-13"));
    CHECK(r.value(0).text == QLatin1String("4006381333931"));
    CHECK(r.value(0).outline.boundingRect().intersects(QRect(300, 400, 500, 260)));
}

static void testOrientation()
{
    if (!QFile::exists(tessdataDir() + QStringLiteral("/osd.traineddata"))) {
        std::printf("orientation test skipped (no osd.traineddata)\n");
        return;
    }
    QImage page(1700, 2200, QImage::Format_RGB888);
    page.fill(Qt::white);
    QPainter p(&page);
    QFont f(QStringLiteral("DejaVu Serif"));
    f.setPixelSize(38);
    p.setFont(f);
    p.setPen(Qt::black);
    const QString line = QStringLiteral("The quick brown fox jumps over the lazy dog while reading");
    for (int i = 0; i < 40; ++i)
        p.drawText(120, 160 + i * 50, line);
    p.end();
    const int up = detectUprightRotation(page);
    const int down = detectUprightRotation(rotateImage(page, 180));
    const int side = detectUprightRotation(rotateImage(page, 90));
    std::printf("orientation: upright %d, upside down %d, turned right %d\n", up, down, side);
    CHECK(up == 0);
    CHECK(down == 180);
    CHECK(side == 270);
}

static void testNewTools()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    // Naming pattern: next number after the highest one already in the folder.
    CHECK(patternName(QStringLiteral("image#"), 4, 7) == QLatin1String("image0007"));
    CHECK(patternName(QStringLiteral("Book-#-scan"), 2, 3) == QLatin1String("Book-03-scan"));
    ScanOptions opt;
    opt.outputDir = dir.path();
    opt.namePattern = QStringLiteral("image#");
    opt.nameDigits = 3;
    {
        QFile placeholder(dir.filePath(QStringLiteral("image005.jpg")));
        CHECK(placeholder.open(QIODevice::WriteOnly));
    }
    const QString next = newScanPath(opt, QStringLiteral("jpg"));
    CHECK(QFileInfo(next).fileName() == QLatin1String("image006.jpg"));
    CHECK(QFileInfo(newScanPath(opt, QStringLiteral("jpg"))).fileName() == QLatin1String("image007.jpg"));

    // Resolution written into JPEG and PNG files.
    QImage page = syntheticPage(nullptr);
    const QByteArray j = jpegWithDpi(encodeJpeg(page, 85), 400);
    QImage back;
    CHECK(back.loadFromData(j) && imageDpi(back) == 400);
    const QString png = dir.filePath(QStringLiteral("dpi.png"));
    CHECK(saveImage(page, png, 90, 250) && imageDpi(loadScan(png)) == 250);
    const QString bmp = dir.filePath(QStringLiteral("x.bmp"));
    CHECK(saveImage(page, bmp, 90, 300) && QImage(bmp).size() == page.size());

    // Kept camera picture: the scan names it, the name survives a re-save.
    ScanOptions keep;
    keep.outputDir = dir.path();
    keep.pageMode = PageMode::FlatPage;
    keep.keepOriginal = true;
    keep.rotation = 90;
    const ScanResult r = processAndSave(encodeJpeg(page, 90), keep);
    CHECK(r.error.isEmpty());
    int rotation = -1;
    const QString original = originalOf(r.path, &rotation);
    CHECK(!original.isEmpty() && QFileInfo::exists(original) && rotation == 90);
    CHECK(saveImage(rotateImage(loadScan(r.path), 90), r.path, 90));
    CHECK(originalOf(r.path) == original);

    // Precise crop.
    const QImage w(400, 300, QImage::Format_RGB32);
    Margins m;
    m.top = 10;
    m.bottom = 20;
    m.left = 30;
    m.right = 40;
    CHECK(cropMargins(w, m, true).size() == QSize(330, 270));
    QImage black(400, 300, QImage::Format_RGB32);
    black.fill(Qt::black);
    const QImage filled = cropMargins(black, m, false);
    CHECK(filled.size() == black.size() && filled.pixel(5, 150) == qRgb(255, 255, 255) &&
          filled.pixel(200, 150) == qRgb(0, 0, 0));

    // Facing pages with a dark gutter at 45 %, and a portrait page that stays whole.
    QImage spread(2000, 1300, QImage::Format_RGB32);
    spread.fill(QColor(240, 240, 240));
    {
        QPainter p(&spread);
        p.fillRect(880, 0, 40, 1300, QColor(70, 70, 70));
    }
    const QVector<QImage> halves = splitFacingPages(spread);
    CHECK(halves.size() == 2);
    if (halves.size() == 2)
        CHECK(std::abs(halves[0].width() - 900) < 40 && halves[0].width() + halves[1].width() <= 2000);
    CHECK(splitFacingPages(page.width() < page.height() ? page : rotateImage(page, 90)).size() == 1);

    // Table from OCR words: three columns, two rows.
    QVector<OcrWord> words;
    const auto add = [&words](int x, int y, const char *t, int line) {
        words.append({QRect(x, y, 80, 30), QString::fromLatin1(t), line});
    };
    add(100, 100, "Name", 0);
    add(400, 100, "Qty", 0);
    add(700, 100, "Price", 0);
    add(100, 150, "Apples", 1);
    add(400, 150, "3", 1);
    add(700, 150, "1.20", 1);
    const QVector<QStringList> table = tableFromWords(words, {});
    CHECK(table.size() == 2);
    if (table.size() == 2)
        CHECK(table[1] == QStringList({QStringLiteral("Apples"), QStringLiteral("3"), QStringLiteral("1.20")}));
    const QByteArray xlsx = xlsxFromSheets({{QStringLiteral("Page 1"), table}});
    CHECK(xlsx.startsWith("PK") && xlsx.contains("xl/worksheets/sheet1.xml") && xlsx.contains("Apples"));
}

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    testProtocol();
    testJpeg();
    testPageDetector();
    testColorModes();
    testPipelineAndExport();
    testOcr();
    testBook();
    testAutoScan();
    testBatchTools();
    testRecorder();
    testInsertionNames();
    testHandwriting();
    testPunchHoles();
    testPaperCorners();
    testBarcode();
    testOrientation();
    testNewTools();
    std::printf(g_failures ? "%d failure(s)\n" : "all tests passed\n", g_failures);
    return g_failures ? 1 : 0;
}
