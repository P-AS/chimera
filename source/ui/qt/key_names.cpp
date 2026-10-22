#include "key_names.hpp"

namespace chimera::qt {

/* The names Chimera has always given keys (the C# DistinctKeyNameOverrides),
 * so a package's default_keybinds.json means the same here. Left and right
 * Shift, Ctrl and Alt arrive merged, as Chimera's default does. */
QString keyCombination(int key, Qt::KeyboardModifiers modifiers)
{
	QString n = keyName(key, modifiers);
	if (n.isEmpty() || n == QStringLiteral("Shift") || n == QStringLiteral("Ctrl") || n == QStringLiteral("Alt")) return n;
	if (modifiers.testFlag(Qt::ShiftModifier)) n.prepend(QStringLiteral("Shift+"));
	if (modifiers.testFlag(Qt::AltModifier)) n.prepend(QStringLiteral("Alt+"));
	if (modifiers.testFlag(Qt::ControlModifier)) n.prepend(QStringLiteral("Ctrl+"));
	return n;
}

QString keyName(int key, Qt::KeyboardModifiers modifiers)
{
	const bool keypad = modifiers.testFlag(Qt::KeypadModifier);
	if (key >= Qt::Key_A && key <= Qt::Key_Z) return QString(QChar('A' + (key - Qt::Key_A)));
	if (key >= Qt::Key_0 && key <= Qt::Key_9)
		return (keypad ? QStringLiteral("Keypad") : QStringLiteral("Number")) + QString::number(key - Qt::Key_0);
	if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return QStringLiteral("F") + QString::number(key - Qt::Key_F1 + 1);
	switch (key)
	{
	case Qt::Key_Up: return QStringLiteral("Up");
	case Qt::Key_Down: return QStringLiteral("Down");
	case Qt::Key_Left: return QStringLiteral("Left");
	case Qt::Key_Right: return QStringLiteral("Right");
	case Qt::Key_Return: return QStringLiteral("Enter");
	case Qt::Key_Enter: return keypad ? QStringLiteral("KeypadEnter") : QStringLiteral("Enter");
	case Qt::Key_Space: return QStringLiteral("Space");
	case Qt::Key_Tab: return QStringLiteral("Tab");
	case Qt::Key_Backspace: return QStringLiteral("Backspace");
	case Qt::Key_Escape: return QStringLiteral("Escape");
	case Qt::Key_Insert: return QStringLiteral("Insert");
	case Qt::Key_Delete: return QStringLiteral("Delete");
	case Qt::Key_Home: return QStringLiteral("Home");
	case Qt::Key_End: return QStringLiteral("End");
	case Qt::Key_PageUp: return QStringLiteral("PageUp");
	case Qt::Key_PageDown: return QStringLiteral("PageDown");
	case Qt::Key_Pause: return QStringLiteral("Pause");
	case Qt::Key_CapsLock: return QStringLiteral("CapsLock");
	case Qt::Key_NumLock: return QStringLiteral("NumLock");
	case Qt::Key_ScrollLock: return QStringLiteral("ScrollLock");
	case Qt::Key_Shift: return QStringLiteral("Shift");
	case Qt::Key_Control: return QStringLiteral("Ctrl");
	case Qt::Key_Alt: return QStringLiteral("Alt");
	case Qt::Key_Semicolon: return QStringLiteral("Semicolon");
	case Qt::Key_Equal: return QStringLiteral("Equals");
	case Qt::Key_Comma: return QStringLiteral("Comma");
	case Qt::Key_Minus: return keypad ? QStringLiteral("KeypadSubtract") : QStringLiteral("Minus");
	case Qt::Key_Period: return keypad ? QStringLiteral("KeypadDecimal") : QStringLiteral("Period");
	case Qt::Key_Slash: return keypad ? QStringLiteral("KeypadDivide") : QStringLiteral("Slash");
	case Qt::Key_Asterisk: return QStringLiteral("KeypadMultiply");
	case Qt::Key_Plus: return QStringLiteral("KeypadAdd");
	case Qt::Key_QuoteLeft: return QStringLiteral("Backtick");
	case Qt::Key_BracketLeft: return QStringLiteral("LeftBracket");
	case Qt::Key_BracketRight: return QStringLiteral("RightBracket");
	case Qt::Key_Backslash: return QStringLiteral("Backslash");
	case Qt::Key_Apostrophe: return QStringLiteral("Apostrophe");
	default: return {};
	}
}

} // namespace chimera::qt
