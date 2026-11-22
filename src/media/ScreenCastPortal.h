// SPDX-License-Identifier: GPL-3.0-or-later
// Screen sharing on Wayland through the desktop portal (org.freedesktop.portal.ScreenCast):
// the desktop asks the user which screen or window to share, then hands out a PipeWire
// stream that GStreamer's pipewiresrc can read.
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace lensscan {

class ScreenCastPortal : public QObject {
    Q_OBJECT
public:
    explicit ScreenCastPortal(QObject *parent = nullptr);
    ~ScreenCastPortal() override;

    static bool isAvailable();
    // Asks the user for a screen; emits started() or failed().
    void start();
    // A new descriptor of the PipeWire connection of the running session (-1 on error).
    // The caller owns it.
    int openRemote(QString *error) const;
    quint32 node() const { return m_node; }
    bool isActive() const { return !m_session.isEmpty() && m_node != 0; }
    void close();

Q_SIGNALS:
    void started(int pipewireFd, quint32 node);
    void failed(const QString &error); // empty: the user cancelled

private Q_SLOTS:
    void onResponse(uint response, const QVariantMap &results);

private:
    enum class Step { None, CreateSession, SelectSources, Start };
    bool request(const QString &method, const QVariantList &args, QVariantMap options, Step step);
    void fail(const QString &error);

    Step m_step = Step::None;
    QString m_requestPath;
    QString m_session;
    quint32 m_node = 0;
    int m_token = 0;
};

} // namespace lensscan
