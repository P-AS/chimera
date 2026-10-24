#include "theme_palette.hpp"

#include <QColor>
#include <QGuiApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QQuickWindow>
#include <QWindow>

#include <iterator>

namespace chimera::qt {

namespace {
QHash<QString, QColor> &current()
{
	static QHash<QString, QColor> colours;
	return colours;
}

/* the platform's palette, as it was before any theme */
const QPalette &platform()
{
	static const QPalette p = QGuiApplication::palette();
	return p;
}

/* Qt Quick's controls take their colours from the platform's palette, not
 * from the application's: a window hands its controls only the colours set
 * on its own palette. So each window is given the theme's colours as its
 * own - all of them, in each of the three states - and every control in it,
 * its menus and its dialogs inherit them. A window that sets its own
 * colours ("unthemed": the Lua forms, in the platform's) is left alone. */
void paintWindow(QQuickWindow *window)
{
	if (window->property("unthemed").toBool()) return;
	QObject *palette = window->property("palette").value<QObject *>();
	if (!palette) return;
	static const char *const names[] = {"window", "windowText", "base", "alternateBase", "text", "button", "buttonText",
		"brightText", "light", "midlight", "mid", "dark", "shadow", "highlight", "highlightedText", "link", "linkVisited",
		"toolTipBase", "toolTipText", "placeholderText", "accent"};
	static const QPalette::ColorRole roles[] = {QPalette::Window, QPalette::WindowText, QPalette::Base, QPalette::AlternateBase,
		QPalette::Text, QPalette::Button, QPalette::ButtonText, QPalette::BrightText, QPalette::Light, QPalette::Midlight,
		QPalette::Mid, QPalette::Dark, QPalette::Shadow, QPalette::Highlight, QPalette::HighlightedText, QPalette::Link,
		QPalette::LinkVisited, QPalette::ToolTipBase, QPalette::ToolTipText, QPalette::PlaceholderText, QPalette::Accent};
	static_assert(std::size(names) == std::size(roles));
	const QPalette app = QGuiApplication::palette();
	const struct
	{
		const char *name;
		QPalette::ColorGroup group;
	} groups[] = {{"active", QPalette::Active}, {"inactive", QPalette::Inactive}, {"disabled", QPalette::Disabled}};
	for (const auto &g : groups)
		if (QObject *colours = palette->property(g.name).value<QObject *>())
			for (size_t i = 0; i < std::size(names); i++)
			{
				const bool known = colours->setProperty(names[i], app.color(g.group, roles[i]));
				Q_ASSERT(known);
			}
}

/* each Qt Quick window, as it is first shown */
class WindowPainter : public QObject
{
public:
	using QObject::QObject;
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		if (event->type() == QEvent::Show)
			if (auto *window = qobject_cast<QQuickWindow *>(watched)) paintWindow(window);
		return false;
	}
};
} // namespace

const QPalette &platformPalette()
{
	return platform();
}

QColor resolveColor(const QString &value)
{
	if (!value.startsWith(QStringLiteral("system:"))) return QColor(value);
	const QString name = value.mid(7).trimmed().toLower();
	const QPalette &p = platform();
	/* the desktop colours Chimera's themes name (WinForms' names) on Qt's;
	 * Control is white smoke off Windows, as Chimera has always had it */
	if (name == QStringLiteral("control") || name == QStringLiteral("menu") || name == QStringLiteral("activeborder"))
	{
#ifdef _WIN32
		return p.color(QPalette::Window);
#else
		return name == QStringLiteral("control") ? QColor(0xF5, 0xF5, 0xF5) : p.color(QPalette::Window);
#endif
	}
	if (name == QStringLiteral("controltext") || name == QStringLiteral("menutext")) return p.color(QPalette::WindowText);
	if (name == QStringLiteral("controldark") || name == QStringLiteral("buttonshadow")) return p.color(QPalette::Mid);
	if (name == QStringLiteral("controldarkdark")) return p.color(QPalette::Dark);
	if (name == QStringLiteral("controllight")) return p.color(QPalette::Midlight);
	if (name == QStringLiteral("controllightlight") || name == QStringLiteral("buttonhighlight")) return p.color(QPalette::Light);
	if (name == QStringLiteral("graytext")) return p.color(QPalette::Disabled, QPalette::Text);
	if (name == QStringLiteral("hottrack")) return p.color(QPalette::Link);
	if (name == QStringLiteral("window")) return p.color(QPalette::Base);
	if (name == QStringLiteral("windowtext")) return p.color(QPalette::Text);
	if (name == QStringLiteral("highlight")) return p.color(QPalette::Highlight);
	if (name == QStringLiteral("highlighttext")) return p.color(QPalette::HighlightedText);
	if (name == QStringLiteral("info")) return p.color(QPalette::ToolTipBase);
	if (name == QStringLiteral("infotext")) return p.color(QPalette::ToolTipText);
	if (name == QStringLiteral("buttonface")) return p.color(QPalette::Button);
	return QColor();
}

QColor ThemeColors::platform(const QString &role) const
{
	static const QHash<QString, QPalette::ColorRole> roles{{QStringLiteral("window"), QPalette::Window},
		{QStringLiteral("windowText"), QPalette::WindowText}, {QStringLiteral("base"), QPalette::Base},
		{QStringLiteral("text"), QPalette::Text}, {QStringLiteral("button"), QPalette::Button},
		{QStringLiteral("buttonText"), QPalette::ButtonText}, {QStringLiteral("highlight"), QPalette::Highlight},
		{QStringLiteral("highlightedText"), QPalette::HighlightedText}, {QStringLiteral("mid"), QPalette::Mid},
		{QStringLiteral("dark"), QPalette::Dark}, {QStringLiteral("placeholderText"), QPalette::PlaceholderText}};
	const auto it = roles.find(role);
	return it == roles.end() ? QColor() : platformPalette().color(it.value());
}

QColor themeColor(const char *role)
{
	return current().value(QString::fromLatin1(role));
}

void applyTheme(const QString &colorsJson)
{
	QHash<QString, QColor> &colours = current();
	colours.clear();
	const QJsonObject o = QJsonDocument::fromJson(colorsJson.toUtf8()).object();
	platform();
	for (auto it = o.begin(); it != o.end(); ++it) colours.insert(it.key(), resolveColor(it.value().toString()));
	if (colours.isEmpty()) return;
	auto c = [&](const char *role, QColor fallback) {
		const QColor v = colours.value(QString::fromLatin1(role));
		return v.isValid() ? v : fallback;
	};
	/* Chimera's roles on Qt's: windows, fields, buttons, the selection,
	 * tooltips (the menus' colours), and what is disabled */
	QPalette p;
	const QColor window = c("WindowBackground", p.color(QPalette::Window)), text = c("WindowText", p.color(QPalette::WindowText));
	p.setColor(QPalette::Window, window);
	p.setColor(QPalette::WindowText, text);
	p.setColor(QPalette::Base, c("InputBackground", window));
	p.setColor(QPalette::AlternateBase, c("AlternateRowBackground", window));
	p.setColor(QPalette::Text, c("InputText", text));
	p.setColor(QPalette::Button, c("ButtonBackground", window));
	p.setColor(QPalette::ButtonText, c("ButtonText", text));
	p.setColor(QPalette::Highlight, c("Selection", QColor(0x3d, 0x8b, 0xff)));
	p.setColor(QPalette::HighlightedText, c("SelectionText", Qt::white));
	p.setColor(QPalette::ToolTipBase, c("MenuBackground", window));
	p.setColor(QPalette::ToolTipText, c("MenuText", text));
	p.setColor(QPalette::PlaceholderText, c("MutedText", text));
	p.setColor(QPalette::Link, c("LinkText", text));
	p.setColor(QPalette::Mid, c("Border", window));
	p.setColor(QPalette::Dark, c("ButtonBorder", window));
	/* the menus' dividers (MenuDivider.qml); Fusion draws only a split handle under the pointer in midlight */
	p.setColor(QPalette::Midlight, c("MenuSeparator", QPalette().color(QPalette::Mid)));
	p.setColor(QPalette::Shadow, c("GlyphShadow", Qt::black));
	for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
		p.setColor(QPalette::Disabled, r, c("DisabledText", text));
	for (QPalette::ColorRole r : {QPalette::Base, QPalette::Button})
		p.setColor(QPalette::Disabled, r, c("DisabledBackground", window));
	QGuiApplication::setPalette(p);
	static WindowPainter *painter = nullptr;
	if (!painter)
	{
		painter = new WindowPainter(qApp);
		qApp->installEventFilter(painter);
	}
	/* the windows open now take it, and repaint */
	for (QWindow *w : QGuiApplication::allWindows())
	{
		if (auto *quick = qobject_cast<QQuickWindow *>(w)) paintWindow(quick);
		w->requestUpdate();
	}
}

} // namespace chimera::qt
