// SPDX-License-Identifier: GPL-3.0-or-later
// Keyboard shortcuts that the user can change (Settings > Shortcuts). Windows bind
// actions by id; changing a key updates every bound QShortcut at once.
#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QShortcut>
#include <QString>

#include <functional>

class QWidget;

namespace lensscan {

struct ShortcutDef {
    QString id;
    QString group;       // translated window name
    QString text;        // translated action name
    QKeySequence defaultKey;
};

class Shortcuts : public QObject {
    Q_OBJECT
public:
    static Shortcuts &instance();

    QList<ShortcutDef> definitions() const;
    QKeySequence key(const QString &id) const;
    // Saves the keys (missing ids keep their value) and updates all bound shortcuts.
    void setKeys(const QHash<QString, QKeySequence> &keys);
    // A window shortcut on parent that calls fn.
    QShortcut *bind(const QString &id, QWidget *parent, const std::function<void()> &fn);
    // Pairs of actions of the same window that would share a key; empty = OK.
    static QStringList conflicts(const QHash<QString, QKeySequence> &keys);

Q_SIGNALS:
    void changed();

private:
    Shortcuts();
    QHash<QString, QKeySequence> m_keys;
    QList<QPair<QString, QPointer<QShortcut>>> m_bound;
};

} // namespace lensscan
