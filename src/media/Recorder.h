// SPDX-License-Identifier: GPL-3.0-or-later
// Video recording with sound through the ffmpeg program (an optional dependency).
// Camera view: frames are JPEG-encoded in a worker thread and written at a constant
// frame rate to ffmpeg's standard input. Screen: ffmpeg's x11grab on X11; on Wayland
// the PipeWire stream given by the desktop portal, read by GStreamer's pipewiresrc
// and piped to ffmpeg as JPEG frames. ffmpeg records a PulseAudio / PipeWire
// microphone at the same time and writes an MP4 or MKV (H.264 + AAC) file.
// Pausing ends the current part of the file; stop() joins the parts.
#pragma once

#include <QImage>
#include <QList>
#include <QMutex>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWaitCondition>

#include <atomic>
#include <thread>

namespace lensscan {

struct AudioSource {
    QString name;        // PulseAudio source name, as given to ffmpeg -f pulse -i
    QString description; // human readable
    bool camera = false; // the document camera's own microphone
};

class Recorder {
public:
    Recorder();
    ~Recorder();

    static QString ffmpegPath(); // empty when ffmpeg is not installed
    // Microphones (monitors of outputs excluded), the camera's first.
    static QList<AudioSource> audioSources();
    // The video encoder ffmpeg offers: libx264, then libopenh264, then mpeg4.
    static QString videoEncoder();

    // Why the screen cannot be recorded on this desktop; empty when it can.
    static QString screenRecordingProblem();
    static bool isWayland();

    // size is rounded down to even numbers. audioSource empty = no sound.
    bool start(const QString &path, QSize size, int fps, const QString &audioSource, QString *error);
    // Whole screen (X11: pipewireFd -1), or the PipeWire stream `node` reached through
    // pipewireFd (Wayland; the recorder takes ownership of the descriptor).
    bool startScreen(const QString &path, int fps, const QString &audioSource, int pipewireFd, quint32 node,
                     QString *error);
    // Ends the current part (blocks up to a few seconds). Wayland screen recordings need
    // a new PipeWire descriptor to resume.
    bool pause(QString *error);
    bool resume(QString *error, int pipewireFd = -1);
    bool isPaused() const { return m_paused; }
    bool isScreen() const { return m_mode != Mode::Frames; }
    // Thread-safe, never blocks: the latest frame is what gets recorded.
    void addFrame(const QImage &frame);
    // Finishes the file (waits for ffmpeg, a few seconds at most).
    bool stop(QString *error);
    bool isRecording() const { return m_running || m_paused; }
    bool isCapturing() const { return m_running; } // recording and not paused
    qint64 elapsedMs() const;
    QSize size() const { return m_size; }

private:
    enum class Mode { Frames, X11Screen, PipeWire };
    void writerLoop();
    bool startSegment(QString *error);
    bool finishSegment(QString *error);
    bool joinSegments(QString *error);

    Mode m_mode = Mode::Frames;
    QString m_path, m_audio;
    QStringList m_segments;
    qint64 m_elapsedBefore = 0; // ms recorded in the finished parts
    bool m_paused = false;
    int m_pwFd = -1;
    quint32 m_pwNode = 0;
    int m_sourcePid = -1; // gst-launch-1.0 for PipeWire
    QSize m_size;
    int m_fps = 25;
    int m_pid = -1;
    int m_fd = -1;
    QString m_logPath;
    std::thread m_writer;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_writeFailed{false};
    qint64 m_startMs = 0;
    QMutex m_mutex;
    QWaitCondition m_cond;
    QImage m_latest;
    bool m_hasNew = false;
};

} // namespace lensscan
