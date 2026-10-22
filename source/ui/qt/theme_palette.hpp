#pragma once

#include <QColor>
#include <QObject>
#include <QString>

namespace chimera::qt {

/* A Chimera theme's colours ({"Role": "#RRGGBB" or "system:Name"}) made the
 * application's palette, and kept for the items that draw their own
 * colours (the piano roll's TAStudio colours). "{}" leaves the platform's
 * palette. */
void applyTheme(const QString &colorsJson);
/* a colour of the theme in use, or an invalid colour when it has none */
QColor themeColor(const char *role);
/* a theme's colour as written: "#RRGGBB", or a desktop colour by the name
 * Chimera's themes use ("system:Control"), which is the platform's own */
QColor resolveColor(const QString &value);

/* the same, for QML */
class ThemeColors : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;
	Q_INVOKABLE QColor resolve(const QString &value) const { return resolveColor(value); }
};

} // namespace chimera::qt
