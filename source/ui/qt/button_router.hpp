#pragma once
/* The pointer's buttons over the picture (WMouse L, M, R, 1, 2) taken as
 * keys are: to the machine's buttons they are bound to, to a hotkey bound
 * to them (Chimera's Capture Mouse is WMouse M), or both, as Key Priority
 * says. QML says a button went down or up; the window's key routing does
 * the rest. */

#include <QObject>
#include <QString>

#include <functional>

namespace chimera::qt {

class ButtonRouter : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;
	std::function<void(const QString &name, bool down)> route;
	Q_INVOKABLE void press(const QString &name, bool down)
	{
		if (route) route(name, down);
	}
};

} // namespace chimera::qt
