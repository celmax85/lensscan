// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScreenCastPortal.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>

#include <unistd.h>

namespace lensscan {

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kScreenCast = QStringLiteral("org.freedesktop.portal.ScreenCast");

QDBusMessage screenCastCall(const QString &method)
{
    return QDBusMessage::createMethodCall(kService, kPath, kScreenCast, method);
}

} // namespace

ScreenCastPortal::ScreenCastPortal(QObject *parent) : QObject(parent) {}

ScreenCastPortal::~ScreenCastPortal()
{
    close();
}

bool ScreenCastPortal::isAvailable()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface())
        return false;
    QDBusMessage get = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    get << kScreenCast << QStringLiteral("version");
    const QDBusMessage reply = bus.call(get, QDBus::Block, 5000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

bool ScreenCastPortal::request(const QString &method, const QVariantList &args, QVariantMap options, Step step)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    // The portal answers on a Request object whose path is known in advance; listen
    // to it before calling, so the answer cannot be missed.
    const QString token = QStringLiteral("lensscan%1").arg(++m_token);
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    if (!m_requestPath.isEmpty())
        bus.disconnect(kService, m_requestPath, QStringLiteral("org.freedesktop.portal.Request"),
                       QStringLiteral("Response"), this, SLOT(onResponse(uint, QVariantMap)));
    m_requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect(kService, m_requestPath, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                this, SLOT(onResponse(uint, QVariantMap)));
    options.insert(QStringLiteral("handle_token"), token);
    QDBusMessage call = screenCastCall(method);
    QVariantList all = args;
    all << options;
    call.setArguments(all);
    const QDBusMessage reply = bus.call(call, QDBus::Block, 10000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        fail(tr("The desktop refused to share the screen: %1").arg(reply.errorMessage()));
        return false;
    }
    m_step = step;
    return true;
}

void ScreenCastPortal::start()
{
    close();
    QVariantMap opt;
    opt.insert(QStringLiteral("session_handle_token"), QStringLiteral("lensscan%1").arg(++m_token));
    request(QStringLiteral("CreateSession"), {}, opt, Step::CreateSession);
}

void ScreenCastPortal::onResponse(uint response, const QVariantMap &results)
{
    const Step step = m_step;
    m_step = Step::None;
    if (response == 1) { // cancelled by the user
        close();
        Q_EMIT failed(QString());
        return;
    }
    if (response != 0) {
        fail(tr("The desktop could not share the screen."));
        return;
    }
    switch (step) {
    case Step::CreateSession: {
        m_session = results.value(QStringLiteral("session_handle")).toString();
        if (m_session.isEmpty()) {
            fail(tr("The desktop did not open a screen sharing session."));
            return;
        }
        QVariantMap opt;
        opt.insert(QStringLiteral("types"), uint(1 | 2)); // monitors and windows
        opt.insert(QStringLiteral("multiple"), false);
        // Cursor drawn into the picture when the desktop can do it.
        QDBusMessage get = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                          QStringLiteral("Get"));
        get << kScreenCast << QStringLiteral("AvailableCursorModes");
        const QDBusMessage modes = QDBusConnection::sessionBus().call(get, QDBus::Block, 5000);
        if (modes.type() == QDBusMessage::ReplyMessage && !modes.arguments().isEmpty() &&
            (modes.arguments().first().value<QDBusVariant>().variant().toUInt() & 2))
            opt.insert(QStringLiteral("cursor_mode"), uint(2));
        request(QStringLiteral("SelectSources"), {QVariant::fromValue(QDBusObjectPath(m_session))}, opt,
                Step::SelectSources);
        break;
    }
    case Step::SelectSources:
        request(QStringLiteral("Start"), {QVariant::fromValue(QDBusObjectPath(m_session)), QString()}, {}, Step::Start);
        break;
    case Step::Start: {
        // streams: a(ua{sv}), the PipeWire node of each shared screen.
        const QDBusArgument streams = results.value(QStringLiteral("streams")).value<QDBusArgument>();
        m_node = 0;
        streams.beginArray();
        while (!streams.atEnd()) {
            quint32 node = 0;
            QVariantMap props;
            streams.beginStructure();
            streams >> node >> props;
            streams.endStructure();
            if (m_node == 0)
                m_node = node;
        }
        streams.endArray();
        if (m_node == 0) {
            fail(tr("No screen was shared."));
            return;
        }
        QString err;
        const int fd = openRemote(&err);
        if (fd < 0) {
            fail(err);
            return;
        }
        Q_EMIT started(fd, m_node);
        break;
    }
    case Step::None:
        break;
    }
}

int ScreenCastPortal::openRemote(QString *error) const
{
    if (m_session.isEmpty())
        return -1;
    QDBusMessage call = screenCastCall(QStringLiteral("OpenPipeWireRemote"));
    call << QVariant::fromValue(QDBusObjectPath(m_session)) << QVariantMap();
    const QDBusReply<QDBusUnixFileDescriptor> reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 10000);
    if (!reply.isValid() || !reply.value().isValid()) {
        if (error)
            *error = tr("The desktop did not give access to the shared screen: %1").arg(reply.error().message());
        return -1;
    }
    return ::dup(reply.value().fileDescriptor()); // QDBusUnixFileDescriptor closes its own copy
}

void ScreenCastPortal::fail(const QString &error)
{
    close();
    Q_EMIT failed(error);
}

void ScreenCastPortal::close()
{
    m_step = Step::None;
    if (!m_session.isEmpty()) {
        QDBusConnection::sessionBus().call(
            QDBusMessage::createMethodCall(kService, m_session, QStringLiteral("org.freedesktop.portal.Session"),
                                           QStringLiteral("Close")),
            QDBus::NoBlock);
        m_session.clear();
    }
    m_node = 0;
}

} // namespace lensscan
