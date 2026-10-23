#pragma once

#include <QGuiApplication>
#include <QObject>
#include <QString>

namespace chimera::qt {

/* Chimera's name for a Qt key, or "" for a key Chimera has no name for */
QString keyName(int key, Qt::KeyboardModifiers modifiers);

/* the same name with the modifiers held before it, as Chimera writes a
 * hotkey ("Ctrl+Shift+V"); a modifier alone is its own name */
QString keyCombination(int key, Qt::KeyboardModifiers modifiers);

/* A key's name for QML, as the window's key filter names it: bare for a
 * machine's button, with the modifiers held for a hotkey. */
class KeyNamer : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;
	Q_INVOKABLE QString name(int key, int modifiers, bool combination) const
	{
		const auto mods = Qt::KeyboardModifiers(modifiers);
		return combination ? keyCombination(key, mods) : keyName(key, mods);
	}
	/* Shift held now, whichever window has the keyboard */
	Q_INVOKABLE bool shiftHeld() const { return QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier); }
};

/* Hands a hotkey whose action is the window's (a menu's, a dialog's) to the
 * window: the key is the hotkeys' to bind, what it does is the menu's. */
class HotkeyRelay : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;
	void press(const std::string &action) { Q_EMIT pressed(QString::fromStdString(action)); }

Q_SIGNALS:
	void pressed(const QString &action);
};

} // namespace chimera::qt
