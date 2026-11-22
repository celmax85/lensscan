// SPDX-License-Identifier: GPL-3.0-or-later
// lensscan-probe: command-line diagnostics for CZUR-compatible document cameras.
#include "device/DeviceDb.h"
#include "device/SonixAsic.h"
#include "device/UsbReset.h"
#include "device/V4l2Device.h"
#include "imaging/Jpeg.h"
#include "imaging/PageDetector.h"
#include "ocr/Ocr.h"

#include <QFile>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <linux/videodev2.h>
#include <string>
#include <thread>

using namespace lensscan;

static void usage()
{
    std::puts("usage: lensscan-probe [--device /dev/videoN] [command]\n"
              "  (no command)          list cameras, formats, controls and extension units\n"
              "  --grab FILE.jpg [WxH] preview/capture/preview/capture cycle on one fd\n"
              "  --stream SECONDS      start the stream once at full resolution, then close\n"
              "  --detect FILE.jpg     run the page detector on an image\n"
              "  --ocr OUT IMAGE... [--lang fra+eng]  text recognition to OUT.pdf, .txt or .docx\n"
              "  --usb-reset           reset the camera's USB port (recovers EPROTO lockups)\n"
              "  --version");
}

static const DeviceInfo *pickDevice(const std::vector<DeviceInfo> &devs, const std::string &node)
{
    for (const auto &d : devs)
        if (node.empty() ? isCzurDevice(d) : d.node == node)
            return &d;
    return nullptr;
}

static void printDevice(const DeviceInfo &d)
{
    auto model = lookupModel(d.vid, d.pid);
    std::printf("%s  %04x:%04x  %s%s\n", d.node.c_str(), d.vid, d.pid, d.name.c_str(),
                isCzurDevice(d) ? "  [CZUR]" : "");
    if (model)
        std::printf("  model: %s\n", model->name);
    for (const auto &xu : d.extensionUnits)
        std::printf("  XU unit %u %s controls=%u\n", xu.unitId, guidToString(xu.guid).c_str(), xu.numControls);
}

int main(int argc, char **argv)
{
    std::string node, cmd, file, size;
    double seconds = 30;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--device" && i + 1 < argc)
            node = argv[++i];
        else if (a == "--grab" && i + 1 < argc) {
            cmd = a;
            file = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-')
                size = argv[++i];
        } else if (a == "--stream" && i + 1 < argc) {
            cmd = a;
            seconds = std::atof(argv[++i]);
        } else if (a == "--restart-test") {
            cmd = a;
        } else if (a == "--detect" && i + 1 < argc) {
            QFile f(QString::fromLocal8Bit(argv[++i]));
            if (!f.open(QIODevice::ReadOnly))
                return 1;
            const QImage img = decodeJpeg(f.readAll());
            const QPolygonF q = PageDetector::detect(img);
            std::printf("%dx%d: ", img.width(), img.height());
            if (q.isEmpty())
                std::puts("no page");
            for (const QPointF &p : q)
                std::printf("(%.0f,%.0f) ", p.x(), p.y());
            std::puts("");
            return 0;
        } else if (a == "--ocr" && i + 2 < argc) {
            // --ocr OUT.{pdf,txt,docx} IMAGE... [--lang fra+eng]
            const QString out = QString::fromLocal8Bit(argv[++i]);
            QStringList images;
            QString lang = ocrDefaultLanguages();
            while (i + 1 < argc) {
                const std::string n = argv[++i];
                if (n == "--lang" && i + 1 < argc)
                    lang = QString::fromLocal8Bit(argv[++i]);
                else
                    images << QString::fromLocal8Bit(n.c_str());
            }
            QString err;
            bool ok;
            if (out.endsWith(QLatin1String(".pdf")))
                ok = ocrToPdf(images, out, lang, QStringLiteral("OCR"), nullptr, &err);
            else if (out.endsWith(QLatin1String(".docx")))
                ok = ocrToDocx(images, out, lang, nullptr, &err);
            else
                ok = ocrToText(images, out, lang, nullptr, &err);
            std::printf("%s (%s): %s\n", qPrintable(out), qPrintable(lang), ok ? "ok" : qPrintable(err));
            return ok ? 0 : 1;
        } else if (a == "--usb-reset") {
            cmd = a;
        } else if (a == "--version") {
            std::puts("lensscan-probe " LENSSCAN_VERSION);
            return 0;
        } else {
            usage();
            return a == "--help" || a == "-h" ? 0 : 2;
        }
    }

    const auto devs = V4l2Device::enumerate();
    if (cmd.empty()) {
        if (devs.empty())
            std::puts("no V4L2 capture device found");
        for (const auto &d : devs) {
            printDevice(d);
            V4l2Device dev;
            if (!dev.open(d.node))
                continue;
            for (const auto &f : dev.formats()) {
                std::printf("  format %s (%s):", fourccToString(f.fourcc).c_str(), f.description.c_str());
                for (const auto &s : f.sizes)
                    std::printf(" %ux%u@%.0f", s.width, s.height, s.fps.empty() ? 0.0 : s.fps.front());
                std::puts("");
            }
            for (const auto &c : dev.controls())
                std::printf("  ctrl %-28s %d [%d..%d] def %d%s\n", c.name.c_str(), c.value, c.minimum,
                            c.maximum, c.defaultValue, c.inactive ? " (inactive)" : "");
        }
        return 0;
    }

    const DeviceInfo *d = pickDevice(devs, node);
    if (!d) {
        std::fprintf(stderr, "no CZUR camera found (use --device)\n");
        return 1;
    }
    if (cmd == "--usb-reset") {
        std::string err;
        if (!usbResetDevice(d->usbSysPath, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        const std::string n = waitForVideoNode(d->vid, d->pid, 8000);
        std::printf("reset done, video node: %s\n", n.empty() ? "(not back yet)" : n.c_str());
        return n.empty() ? 1 : 0;
    }

    V4l2Device dev;
    if (!dev.open(d->node)) {
        std::fprintf(stderr, "%s\n", dev.lastError().c_str());
        return 1;
    }

    if (cmd == "--stream") {
        uint32_t sw = 0, sh = 0;
        for (const auto &f : dev.formats())
            if (f.fourcc == V4L2_PIX_FMT_MJPEG && !f.sizes.empty()) {
                sw = f.sizes.front().width;
                sh = f.sizes.front().height;
            }
        if (const char *env = std::getenv("PROBE_SIZE"))
            std::sscanf(env, "%ux%u", &sw, &sh);
        if (!dev.setFormat(V4L2_PIX_FMT_MJPEG, sw, sh, std::getenv("PROBE_FPS") ? std::atof(std::getenv("PROBE_FPS")) : 15) || !dev.startStreaming(4)) {
            std::printf("STREAMON FAILED: %s\n", dev.lastError().c_str());
            return 1;
        }
        int frames = 0, valid = 0, errors = 0;
        size_t bytes = 0;
        const auto t0 = std::chrono::steady_clock::now();
        const auto end = t0 + std::chrono::duration<double>(seconds);
        while (std::chrono::steady_clock::now() < end) {
            if (dev.readFrame(2000, [&](const uint8_t *p, size_t n) {
                    bytes += n;
                    valid += looksLikeJpeg(p, n) && !decodeJpeg(p, n, 0, true).isNull();
                }))
                ++frames;
            else
                ++errors;
        }
        std::printf("valid JPEG frames: %d, flagged/failed buffers: %d\n", valid, errors);
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("streamed %ux%u: %d frames in %.1f s (%.1f fps, avg %zu KB)\n", dev.width(), dev.height(),
                    frames, el, frames / el, frames ? bytes / frames / 1024 : 0);
        dev.close();
        return frames > 0 ? 0 : 1;
    }

    if (cmd == "--restart-test") {
        // Restarts the stream on the same fd with pauses, from least to most risky.
        struct Step { uint32_t w, h; int pauseMs; const char *what; };
        const Step steps[] = {
            {3264, 2448, 0, "start full-res"},
            {3264, 2448, 1500, "restart same size after 1.5 s"},
            {3264, 2448, 1500, "restart same size again after 1.5 s"},
            {1280, 960, 1500, "switch to 1280x960 after 1.5 s"},
            {3264, 2448, 1500, "switch back to full-res after 1.5 s"},
        };
        for (const auto &st : steps) {
            dev.stopStreaming();
            std::this_thread::sleep_for(std::chrono::milliseconds(st.pauseMs));
            int frames = 0;
            if (dev.setFormat(V4L2_PIX_FMT_MJPEG, st.w, st.h, 15) && dev.startStreaming(4))
                for (int i = 0; i < 20; ++i)
                    frames += dev.readFrame(2000, nullptr) ? 1 : 0;
            std::printf("%-40s %s\n", st.what, frames ? "OK" : ("FAIL: " + dev.lastError()).c_str());
            std::fflush(stdout);
            if (!frames)
                return 1;
        }
        dev.stopStreaming();
        return 0;
    }

    // --grab
    uint32_t w = 0, h = 0;
    if (!size.empty())
        std::sscanf(size.c_str(), "%ux%u", &w, &h);
    if (!w) {
        for (const auto &f : dev.formats())
            if (f.fourcc == V4L2_PIX_FMT_MJPEG && !f.sizes.empty()) {
                w = f.sizes.front().width;
                h = f.sizes.front().height;
            }
    }
    // Same fd for the whole sequence: preview -> full-res capture -> preview -> capture.
    // Closing and reopening the node can lock this camera, so we never do that mid-session.
    auto stream = [&](uint32_t sw, uint32_t sh, int skip, const std::string &out) -> bool {
        dev.stopStreaming();
        if (!dev.setFormat(V4L2_PIX_FMT_MJPEG, sw, sh) || !dev.startStreaming(3)) {
            std::fprintf(stderr, "%ux%u: %s\n", sw, sh, dev.lastError().c_str());
            return false;
        }
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < skip + 30; ++i) {
            bool got = false;
            dev.readFrame(3000, [&](const uint8_t *p, size_t n) {
                if (i < skip)
                    return;
                if (!out.empty())
                    std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char *>(p), std::streamsize(n));
                got = true;
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
                std::printf("  %ux%u ok after %lld ms (%zu bytes)%s%s\n", dev.width(), dev.height(),
                            static_cast<long long>(ms), n, out.empty() ? "" : " -> ", out.c_str());
            });
            if (got)
                return true;
        }
        std::fprintf(stderr, "%ux%u: no frame (%s)\n", sw, sh, dev.lastError().c_str());
        return false;
    };
    const std::string second = file.substr(0, file.find_last_of('.')) + "-2.jpg";
    std::puts("step 1: preview 1280x960");
    bool ok = stream(1280, 960, 8, "");
    std::printf("step 2: capture %ux%u\n", w, h);
    ok = ok && stream(w, h, 3, file);
    std::puts("step 3: back to preview");
    ok = ok && stream(1280, 960, 3, "");
    std::puts("step 4: second capture");
    ok = ok && stream(w, h, 3, second);
    dev.stopStreaming();
    std::puts(ok ? "all steps OK" : "FAILED");
    return ok ? 0 : 1;
}
