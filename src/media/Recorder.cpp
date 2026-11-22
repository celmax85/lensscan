// SPDX-License-Identifier: GPL-3.0-or-later
#include "Recorder.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace lensscan {

namespace {

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString runFfmpeg(const QStringList &args)
{
    const QString ff = Recorder::ffmpegPath();
    if (ff.isEmpty())
        return {};
    QProcess p;
    p.start(ff, args);
    if (!p.waitForFinished(5000)) {
        p.kill();
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
}

bool writeAll(int fd, const char *data, qint64 size)
{
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size_t(size));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        data += n;
        size -= n;
    }
    return true;
}

// waitpid with a timeout.
bool waitChild(int pid, int timeoutMs, int *status)
{
    for (int waited = 0; waited <= timeoutMs; waited += 50) {
        const int r = ::waitpid(pid, status, WNOHANG);
        if (r == pid)
            return true;
        if (r < 0)
            return true;
        ::usleep(50000);
    }
    return false;
}

} // namespace

Recorder::Recorder() = default;

Recorder::~Recorder()
{
    if (isRecording())
        stop(nullptr);
}

QString Recorder::ffmpegPath()
{
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QList<AudioSource> Recorder::audioSources()
{
    QList<AudioSource> out;
    const QString text = runFfmpeg({QStringLiteral("-hide_banner"), QStringLiteral("-sources"), QStringLiteral("pulse")});
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.startsWith(QLatin1Char('*')))
            line = line.mid(1).trimmed();
        const int open = line.indexOf(QLatin1String(" ["));
        if (open <= 0 || !line.contains(QLatin1Char(']')))
            continue;
        AudioSource s;
        s.name = line.left(open);
        s.description = line.mid(open + 2, line.lastIndexOf(QLatin1Char(']')) - open - 2);
        if (s.name.endsWith(QLatin1String(".monitor")))
            continue; // what the speakers play, not a microphone
        s.camera = s.name.contains(QLatin1String("CZUR"), Qt::CaseInsensitive) ||
                   s.description.contains(QLatin1String("CZUR"), Qt::CaseInsensitive);
        if (s.camera)
            out.prepend(s);
        else
            out.append(s);
    }
    return out;
}

QString Recorder::videoEncoder()
{
    static const QString enc = [] {
        const QString text = runFfmpeg({QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
        for (const char *e : {"libx264", "libopenh264", "mpeg4"})
            if (text.contains(QLatin1Char(' ') + QLatin1String(e) + QLatin1Char(' ')))
                return QString::fromLatin1(e);
        return QString();
    }();
    return enc;
}

bool Recorder::isWayland()
{
    return !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") ||
           qEnvironmentVariable("XDG_SESSION_TYPE").compare(QLatin1String("wayland"), Qt::CaseInsensitive) == 0;
}

QString Recorder::screenRecordingProblem()
{
    static const QString problem = [] {
        if (ffmpegPath().isEmpty() || videoEncoder().isEmpty())
            return QObject::tr("Screen recording needs the ffmpeg program. Install it with your package manager, "
                               "for example: sudo apt install ffmpeg");
        if (isWayland()) {
            const QString inspect = QStandardPaths::findExecutable(QStringLiteral("gst-inspect-1.0"));
            const auto hasElement = [&inspect](const char *e) {
                QProcess p;
                p.start(inspect, {QString::fromLatin1(e)});
                return p.waitForFinished(5000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
            };
            if (QStandardPaths::findExecutable(QStringLiteral("gst-launch-1.0")).isEmpty() || inspect.isEmpty() ||
                !hasElement("pipewiresrc") || !hasElement("jpegenc") || !hasElement("videorate"))
                return QObject::tr("Screen recording on Wayland needs GStreamer and its PipeWire plugin, for example: "
                                   "sudo apt install gstreamer1.0-tools gstreamer1.0-pipewire gstreamer1.0-plugins-good");
            return QString();
        }
        if (qEnvironmentVariableIsEmpty("DISPLAY"))
            return QObject::tr("No graphical display to record.");
        if (!runFfmpeg({QStringLiteral("-hide_banner"), QStringLiteral("-devices")}).contains(QLatin1String("x11grab")))
            return QObject::tr("This ffmpeg cannot record the screen (no x11grab).");
        return QString();
    }();
    return problem;
}

namespace {

// posix_spawn with stdin / stdout from the given descriptors (-1 = /dev/null), stderr
// to a log file, and optionally extraFd as descriptor 3. Returns the pid or -1.
int spawnProcess(const QString &program, const QStringList &args, int stdinFd, int stdoutFd, const QString &logPath,
                 int extraFd, int *spawnError)
{
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (stdinFd >= 0)
        posix_spawn_file_actions_adddup2(&fa, stdinFd, 0);
    else
        posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (stdoutFd >= 0)
        posix_spawn_file_actions_adddup2(&fa, stdoutFd, 1);
    else
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    const QByteArray log = QFile::encodeName(logPath);
    posix_spawn_file_actions_addopen(&fa, 2, log.constData(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (extraFd >= 0)
        posix_spawn_file_actions_adddup2(&fa, extraFd, 3); // dup2 clears close-on-exec
    QList<QByteArray> argBytes;
    argBytes << QFile::encodeName(program);
    for (const QString &a : args)
        argBytes << a.toLocal8Bit();
    std::vector<char *> argv;
    for (QByteArray &a : argBytes)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (spawnError)
        *spawnError = rc;
    return rc == 0 ? int(pid) : -1;
}

// Waits for pid; on timeout sends sig, then SIGKILL. Returns the wait status.
int reap(int pid, int timeoutMs, int sig)
{
    int status = 0;
    if (pid < 0)
        return 0;
    if (!waitChild(pid, timeoutMs, &status)) {
        ::kill(pid, sig);
        if (!waitChild(pid, 5000, &status)) {
            ::kill(pid, SIGKILL);
            waitChild(pid, 2000, &status);
        }
    }
    return status;
}

QString partPath(const QString &path, int n)
{
    const QFileInfo fi(path);
    return fi.dir().filePath(QStringLiteral(".%1.part%2.%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix()));
}

} // namespace

bool Recorder::start(const QString &path, QSize size, int fps, const QString &audioSource, QString *error)
{
    if (isRecording())
        return false;
    m_mode = Mode::Frames;
    m_size = QSize(size.width() & ~1, size.height() & ~1);
    m_fps = std::clamp(fps, 5, 60);
    m_path = path;
    m_audio = audioSource;
    m_segments.clear();
    m_elapsedBefore = 0;
    m_paused = false;
    return startSegment(error);
}

bool Recorder::startScreen(const QString &path, int fps, const QString &audioSource, int pipewireFd, quint32 node,
                           QString *error)
{
    if (isRecording())
        return false;
    m_mode = pipewireFd >= 0 ? Mode::PipeWire : Mode::X11Screen;
    m_pwFd = pipewireFd;
    m_pwNode = node;
    m_size = QSize();
    m_fps = std::clamp(fps, 5, 60);
    m_path = path;
    m_audio = audioSource;
    m_segments.clear();
    m_elapsedBefore = 0;
    m_paused = false;
    return startSegment(error);
}

bool Recorder::startSegment(QString *error)
{
    const QString ff = ffmpegPath();
    const QString enc = videoEncoder();
    if (ff.isEmpty() || enc.isEmpty()) {
        if (error)
            *error = QObject::tr("Video recording needs the ffmpeg program. Install it with your package manager, "
                                 "for example: sudo apt install ffmpeg");
        return false;
    }
    const QString part = partPath(m_path, int(m_segments.size()) + 1);
    m_logPath = QDir::temp().filePath(QStringLiteral("lensscan-ffmpeg-%1.log").arg(::getpid()));
    QFile::remove(m_logPath);

    QStringList args = {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                        QStringLiteral("-y"), QStringLiteral("-thread_queue_size"), QStringLiteral("512")};
    if (m_mode == Mode::X11Screen)
        args << QStringLiteral("-nostdin") << QStringLiteral("-f") << QStringLiteral("x11grab")
             << QStringLiteral("-framerate") << QString::number(m_fps) << QStringLiteral("-draw_mouse")
             << QStringLiteral("1") << QStringLiteral("-i") << qEnvironmentVariable("DISPLAY");
    else
        args << QStringLiteral("-f") << QStringLiteral("mjpeg") << QStringLiteral("-framerate") << QString::number(m_fps)
             << QStringLiteral("-i") << QStringLiteral("pipe:0");
    if (!m_audio.isEmpty())
        args << QStringLiteral("-thread_queue_size") << QStringLiteral("1024") << QStringLiteral("-f")
             << QStringLiteral("pulse") << QStringLiteral("-i") << m_audio;
    args << QStringLiteral("-map") << QStringLiteral("0:v");
    if (!m_audio.isEmpty())
        args << QStringLiteral("-map") << QStringLiteral("1:a");
    if (m_mode != Mode::Frames) // H.264 in 4:2:0 needs even sizes
        args << QStringLiteral("-vf") << QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2");
    args << QStringLiteral("-c:v") << enc;
    if (enc == QLatin1String("libx264"))
        args << QStringLiteral("-preset") << QStringLiteral("veryfast") << QStringLiteral("-crf") << QStringLiteral("23");
    else
        args << QStringLiteral("-b:v") << QStringLiteral("6M");
    args << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    if (!m_audio.isEmpty())
        args << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a") << QStringLiteral("128k")
             << QStringLiteral("-shortest");
    if (QFileInfo(m_path).suffix().compare(QLatin1String("mp4"), Qt::CaseInsensitive) == 0)
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    args << part;

    int pipeFds[2] = {-1, -1};
    if (m_mode != Mode::X11Screen && ::pipe2(pipeFds, O_CLOEXEC) != 0) {
        if (error)
            *error = QString::fromLocal8Bit(strerror(errno));
        return false;
    }
    // A dead ffmpeg must give EPIPE, not kill the application.
    std::signal(SIGPIPE, SIG_IGN);
    int rc = 0;
    m_sourcePid = -1;
    if (m_mode == Mode::PipeWire) {
        const QString gst = QStandardPaths::findExecutable(QStringLiteral("gst-launch-1.0"));
        // keepalive-time repeats the last picture while the screen does not change, so
        // videorate always has frames to keep the rate constant.
        const QStringList gstArgs = {
            QStringLiteral("-e"), QStringLiteral("-q"), QStringLiteral("pipewiresrc"), QStringLiteral("fd=3"),
            QStringLiteral("path=%1").arg(m_pwNode), QStringLiteral("do-timestamp=true"),
            QStringLiteral("keepalive-time=%1").arg(1000 / m_fps), QStringLiteral("always-copy=true"),
            QStringLiteral("!"), QStringLiteral("videoconvert"), QStringLiteral("!"), QStringLiteral("videorate"),
            QStringLiteral("!"), QStringLiteral("video/x-raw,framerate=%1/1").arg(m_fps), QStringLiteral("!"),
            QStringLiteral("jpegenc"), QStringLiteral("quality=85"), QStringLiteral("!"), QStringLiteral("fdsink"),
            QStringLiteral("fd=1"), QStringLiteral("sync=false")};
        m_sourcePid = spawnProcess(gst, gstArgs, -1, pipeFds[1], m_logPath, m_pwFd, &rc);
        ::close(pipeFds[1]);
        ::close(m_pwFd); // gst-launch has its copy; a new one is needed for every part
        m_pwFd = -1;
        if (m_sourcePid < 0) {
            ::close(pipeFds[0]);
            if (error)
                *error = QString::fromLocal8Bit(strerror(rc));
            return false;
        }
        m_pid = spawnProcess(ff, args, pipeFds[0], -1, m_logPath, -1, &rc);
        ::close(pipeFds[0]);
    } else if (m_mode == Mode::X11Screen) {
        m_pid = spawnProcess(ff, args, -1, -1, m_logPath, -1, &rc);
    } else {
        m_pid = spawnProcess(ff, args, pipeFds[0], -1, m_logPath, -1, &rc);
        ::close(pipeFds[0]);
        m_fd = pipeFds[1];
    }
    if (m_pid < 0) {
        if (m_fd >= 0)
            ::close(m_fd);
        m_fd = -1;
        if (m_sourcePid >= 0) {
            ::kill(m_sourcePid, SIGKILL);
            reap(m_sourcePid, 1000, SIGKILL);
            m_sourcePid = -1;
        }
        if (error)
            *error = QString::fromLocal8Bit(strerror(rc));
        return false;
    }
    m_segments << part;
    m_writeFailed = false;
    m_hasNew = false;
    m_latest = QImage();
    m_startMs = nowMs();
    m_running = true;
    if (m_mode == Mode::Frames)
        m_writer = std::thread([this] { writerLoop(); });
    return true;
}

void Recorder::addFrame(const QImage &frame)
{
    if (!m_running || frame.isNull())
        return;
    QMutexLocker lock(&m_mutex);
    m_latest = frame;
    m_hasNew = true;
    m_cond.wakeOne();
}

void Recorder::writerLoop()
{
    QByteArray encoded;
    qint64 written = 0; // frames sent
    while (m_running) {
        QImage frame;
        {
            QMutexLocker lock(&m_mutex);
            if (!m_hasNew && encoded.isEmpty())
                m_cond.wait(&m_mutex, 100);
            if (m_hasNew) {
                frame = m_latest;
                m_hasNew = false;
            }
        }
        if (!frame.isNull()) {
            QImage f = frame.size() == m_size ? frame
                                              : frame.scaled(m_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            QBuffer buf(&encoded);
            encoded.clear();
            buf.open(QIODevice::WriteOnly);
            f.convertToFormat(QImage::Format_RGB888).save(&buf, "JPG", 88);
        }
        if (encoded.isEmpty())
            continue;
        // Constant frame rate: send as many frames as the clock says are due,
        // repeating the latest one while the picture does not change.
        const qint64 due = (nowMs() - m_startMs) * m_fps / 1000 + 1;
        while (written < due && m_running) {
            if (!writeAll(m_fd, encoded.constData(), encoded.size())) {
                m_writeFailed = true;
                m_running = false;
                break;
            }
            ++written;
        }
        const qint64 nextMs = m_startMs + written * 1000 / m_fps;
        const qint64 wait = nextMs - nowMs();
        if (wait > 0) {
            QMutexLocker lock(&m_mutex);
            if (!m_hasNew)
                m_cond.wait(&m_mutex, ulong(wait));
        }
    }
}

bool Recorder::finishSegment(QString *error)
{
    if (m_pid < 0)
        return false;
    const bool wasRunning = m_running;
    m_running = false;
    m_cond.wakeAll();
    if (m_writer.joinable())
        m_writer.join();
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
    int status = 0;
    bool interrupted = false;
    if (m_mode == Mode::Frames) {
        // End of input finishes the file; with a microphone, -shortest stops the audio too.
        status = reap(m_pid, 8000, SIGINT); // ffmpeg finalises the file on SIGINT
    } else if (m_mode == Mode::PipeWire) {
        if (m_sourcePid >= 0) {
            ::kill(m_sourcePid, SIGINT); // -e: end of stream, then gst-launch exits
            reap(m_sourcePid, 5000, SIGTERM);
            m_sourcePid = -1;
        }
        status = reap(m_pid, 8000, SIGINT);
    } else {
        ::kill(m_pid, SIGINT);
        interrupted = true;
        status = reap(m_pid, 8000, SIGTERM);
    }
    m_pid = -1;
    if (wasRunning)
        m_elapsedBefore += nowMs() - m_startMs;
    QFile log(m_logPath);
    QString msg;
    if (log.open(QIODevice::ReadOnly))
        msg = QString::fromLocal8Bit(log.readAll()).trimmed();
    log.remove();
    const QFileInfo part(m_segments.value(m_segments.size() - 1));
    // ffmpeg exits with 255 after SIGINT even when the file is complete.
    const bool exited = WIFEXITED(status) && (WEXITSTATUS(status) == 0 || (interrupted && WEXITSTATUS(status) == 255));
    const bool ok = exited && !m_writeFailed && part.exists() && part.size() > 0;
    if (!ok && error)
        *error = msg.isEmpty() ? QObject::tr("ffmpeg stopped with an error.") : msg.right(600);
    return ok;
}

bool Recorder::joinSegments(QString *error)
{
    QStringList parts;
    for (const QString &p : std::as_const(m_segments))
        if (QFileInfo(p).size() > 1024)
            parts << p;
        else
            QFile::remove(p);
    m_segments.clear();
    if (parts.isEmpty()) {
        if (error)
            *error = QObject::tr("Nothing was recorded.");
        return false;
    }
    QFile::remove(m_path);
    if (parts.size() == 1)
        return QFile::rename(parts.first(), m_path) ||
               (error && (*error = QObject::tr("Cannot write %1").arg(m_path), false));
    // Join the parts without re-encoding them.
    const QString list = QFileInfo(m_path).dir().filePath(QStringLiteral(".%1.parts.txt").arg(QFileInfo(m_path).completeBaseName()));
    QFile lf(list);
    if (!lf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return error && (*error = QObject::tr("Cannot write %1").arg(list), false);
    for (QString p : std::as_const(parts))
        lf.write("file '" + QFile::encodeName(p.replace(QLatin1Char('\''), QStringLiteral("'\\''"))) + "'\n");
    lf.close();
    QStringList args = {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                        QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("concat"), QStringLiteral("-safe"),
                        QStringLiteral("0"), QStringLiteral("-i"), list, QStringLiteral("-c"), QStringLiteral("copy")};
    if (QFileInfo(m_path).suffix().compare(QLatin1String("mp4"), Qt::CaseInsensitive) == 0)
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    args << m_path;
    QProcess p;
    p.start(ffmpegPath(), args);
    const bool ok = p.waitForFinished(120000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    QFile::remove(list);
    if (!ok) {
        if (error)
            *error = QObject::tr("The parts of the recording could not be joined; they are kept as %1.\n%2")
                         .arg(QFileInfo(parts.first()).dir().path(), QString::fromLocal8Bit(p.readAllStandardError()).right(400));
        return false;
    }
    for (const QString &part : std::as_const(parts))
        QFile::remove(part);
    return true;
}

bool Recorder::pause(QString *error)
{
    if (!m_running)
        return false;
    const bool ok = finishSegment(error);
    m_paused = true;
    return ok;
}

bool Recorder::resume(QString *error, int pipewireFd)
{
    if (!m_paused)
        return false;
    if (m_mode == Mode::PipeWire)
        m_pwFd = pipewireFd;
    if (!startSegment(error))
        return false;
    m_paused = false;
    return true;
}

bool Recorder::stop(QString *error)
{
    if (!isRecording())
        return false;
    bool ok = true;
    QString segmentError;
    if (m_running)
        ok = finishSegment(&segmentError);
    m_paused = false;
    if (m_pwFd >= 0) {
        ::close(m_pwFd);
        m_pwFd = -1;
    }
    QString joinError;
    const bool joined = joinSegments(&joinError);
    if (error)
        *error = !ok ? segmentError : joinError;
    return ok && joined;
}

qint64 Recorder::elapsedMs() const
{
    return m_elapsedBefore + (m_running ? nowMs() - m_startMs : 0);
}

} // namespace lensscan
