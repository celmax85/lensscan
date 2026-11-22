// SPDX-License-Identifier: GPL-3.0-or-later
#include "Shortcuts.h"

#include <QCoreApplication>
#include <QSettings>
#include <QWidget>

namespace lensscan {

namespace {

struct RawDef {
    const char *id, *group, *text;
    QKeySequence key;
};

const QList<RawDef> &rawDefs()
{
    static const QList<RawDef> defs = {
        {"main/scan", QT_TRANSLATE_NOOP("Shortcuts", "Main window"), QT_TRANSLATE_NOOP("Shortcuts", "Open the scan window"),
         QKeySequence(Qt::Key_F5)},
        {"main/presentation", QT_TRANSLATE_NOOP("Shortcuts", "Main window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Open the presentation window"), QKeySequence(Qt::Key_F6)},
        {"main/edit", QT_TRANSLATE_NOOP("Shortcuts", "Main window"), QT_TRANSLATE_NOOP("Shortcuts", "Edit page"),
         QKeySequence(Qt::CTRL | Qt::Key_E)},
        {"main/rotateLeft", QT_TRANSLATE_NOOP("Shortcuts", "Main window"), QT_TRANSLATE_NOOP("Shortcuts", "Rotate left"),
         QKeySequence(Qt::CTRL | Qt::Key_L)},
        {"main/rotateRight", QT_TRANSLATE_NOOP("Shortcuts", "Main window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Rotate right"), QKeySequence(Qt::CTRL | Qt::Key_R)},
        {"main/delete", QT_TRANSLATE_NOOP("Shortcuts", "Main window"), QT_TRANSLATE_NOOP("Shortcuts", "Delete"),
         QKeySequence(QKeySequence::Delete)},
        {"main/pdf", QT_TRANSLATE_NOOP("Shortcuts", "Main window"), QT_TRANSLATE_NOOP("Shortcuts", "Save as PDF"),
         QKeySequence(Qt::CTRL | Qt::Key_P)},
        {"main/print", QT_TRANSLATE_NOOP("Shortcuts", "Main window"), QT_TRANSLATE_NOOP("Shortcuts", "Print"),
         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)},
        {"main/insert", QT_TRANSLATE_NOOP("Shortcuts", "Main window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Insert pages before the selection"), QKeySequence(Qt::Key_Insert)},
        {"main/info", QT_TRANSLATE_NOOP("Shortcuts", "Main window"), QT_TRANSLATE_NOOP("Shortcuts", "Image information"),
         QKeySequence(Qt::ALT | Qt::Key_Return)},
        {"main/settings", QT_TRANSLATE_NOOP("Shortcuts", "Main window"), QT_TRANSLATE_NOOP("Shortcuts", "Settings"),
         QKeySequence(Qt::CTRL | Qt::Key_Comma)},
        {"scan/scan", QT_TRANSLATE_NOOP("Shortcuts", "Scan window"), QT_TRANSLATE_NOOP("Shortcuts", "Scan"),
         QKeySequence(Qt::Key_Space)},
        {"scan/rotateLeft", QT_TRANSLATE_NOOP("Shortcuts", "Scan window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Rotate left"), QKeySequence(Qt::CTRL | Qt::Key_L)},
        {"scan/rotateRight", QT_TRANSLATE_NOOP("Shortcuts", "Scan window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Rotate right"), QKeySequence(Qt::CTRL | Qt::Key_R)},
        {"scan/back", QT_TRANSLATE_NOOP("Shortcuts", "Scan window"), QT_TRANSLATE_NOOP("Shortcuts", "Back"),
         QKeySequence(Qt::Key_Escape)},
        {"present/freeze", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Freeze / unfreeze"), QKeySequence(Qt::Key_F)},
        {"present/screenshot", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Screenshot"), QKeySequence(Qt::Key_S)},
        {"present/record", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Start / stop recording"), QKeySequence(Qt::Key_R)},
        {"present/pointer", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Move / zoom tool"), QKeySequence(Qt::Key_M)},
        {"present/pen", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"), QT_TRANSLATE_NOOP("Shortcuts", "Pen"),
         QKeySequence(Qt::Key_P)},
        {"present/highlighter", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Highlighter"), QKeySequence(Qt::Key_H)},
        {"present/eraser", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Eraser"), QKeySequence(Qt::Key_E)},
        {"present/undo", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Undo the last annotation"), QKeySequence(QKeySequence::Undo)},
        {"present/clear", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Remove all annotations"), QKeySequence(Qt::Key_C)},
        {"present/zoomReset", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Reset zoom"), QKeySequence(Qt::Key_0)},
        {"present/fullscreen", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"),
         QT_TRANSLATE_NOOP("Shortcuts", "Full screen"), QKeySequence(Qt::Key_F11)},
        {"present/back", QT_TRANSLATE_NOOP("Shortcuts", "Presentation window"), QT_TRANSLATE_NOOP("Shortcuts", "Back"),
         QKeySequence(Qt::Key_Escape)},
    };
    return defs;
}

QString settingsKey(const QString &id)
{
    return QStringLiteral("shortcuts/") + id;
}

} // namespace

Shortcuts &Shortcuts::instance()
{
    static Shortcuts s;
    return s;
}

Shortcuts::Shortcuts()
{
    QSettings s;
    for (const RawDef &d : rawDefs()) {
        const QString id = QString::fromLatin1(d.id);
        const QVariant v = s.value(settingsKey(id));
        m_keys.insert(id, v.isValid() ? QKeySequence(v.toString(), QKeySequence::PortableText) : d.key);
    }
}

QList<ShortcutDef> Shortcuts::definitions() const
{
    QList<ShortcutDef> out;
    for (const RawDef &d : rawDefs())
        out.append({QString::fromLatin1(d.id), QCoreApplication::translate("Shortcuts", d.group),
                    QCoreApplication::translate("Shortcuts", d.text), d.key});
    return out;
}

QKeySequence Shortcuts::key(const QString &id) const
{
    return m_keys.value(id);
}

void Shortcuts::setKeys(const QHash<QString, QKeySequence> &keys)
{
    QSettings s;
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        if (!m_keys.contains(it.key()))
            continue;
        m_keys[it.key()] = it.value();
        s.setValue(settingsKey(it.key()), it.value().toString(QKeySequence::PortableText));
    }
    for (auto &b : m_bound)
        if (b.second)
            b.second->setKey(m_keys.value(b.first));
    Q_EMIT changed();
}

QShortcut *Shortcuts::bind(const QString &id, QWidget *parent, const std::function<void()> &fn)
{
    auto *sc = new QShortcut(m_keys.value(id), parent);
    sc->setContext(Qt::WindowShortcut);
    QObject::connect(sc, &QShortcut::activated, parent, fn);
    m_bound.removeIf([](const auto &b) { return b.second.isNull(); });
    m_bound.append({id, sc});
    return sc;
}

QStringList Shortcuts::conflicts(const QHash<QString, QKeySequence> &keys)
{
    QStringList out;
    const QList<ShortcutDef> defs = instance().definitions();
    for (int i = 0; i < defs.size(); ++i)
        for (int j = i + 1; j < defs.size(); ++j) {
            if (defs[i].group != defs[j].group)
                continue;
            const QKeySequence a = keys.value(defs[i].id), b = keys.value(defs[j].id);
            if (!a.isEmpty() && a == b)
                out << QStringLiteral("%1: %2 / %3 (%4)")
                           .arg(defs[i].group, defs[i].text, defs[j].text, a.toString(QKeySequence::NativeText));
        }
    return out;
}

} // namespace lensscan
