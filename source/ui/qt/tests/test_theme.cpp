/* A Chimera theme reaches Qt Quick's controls: a window's buttons and
 * fields, its menus, a window opened later, and a theme chosen while they
 * are open; a window that says it is "unthemed" (a Lua form) keeps the
 * colours it set itself.
 *
 * Qt Quick's controls take their colours from the platform's palette, not
 * the application's, so a theme set only as the application's palette
 * leaves them in the desktop's colours: this is what failed before each
 * window was given the theme as its own palette. */
#include "../theme_palette.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>

#include <cstdio>
#include <memory>

namespace {
int checks = 0, failed = 0;

void check(bool ok, const char *what, int line)
{
	checks++;
	if (ok) return;
	failed++;
	std::printf("FAIL %s:%d: %s\n", __FILE__, line, what);
}
#define CHECK(x) check((x), #x, __LINE__)

const char *const kWindow = R"(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    width: 200; height: 120; visible: true
    property bool unthemed: false
    readonly property color windowColour: palette.window
    readonly property color buttonColour: button.palette.button
    readonly property color fieldColour: field.palette.base
    readonly property color menuColour: menu.palette.window
    Column {
        Button { id: button; text: "B" }
        TextField { id: field; text: "F" }
    }
    Menu { id: menu; MenuItem { text: "M" } }
}
)";

const char *const kUnthemed = R"(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    width: 200; height: 120; visible: true
    readonly property bool unthemed: true
    palette.button: "#123456"
    readonly property color buttonColour: button.palette.button
    readonly property color windowColour: palette.window
    Button { id: button; text: "B" }
}
)";

std::unique_ptr<QObject> make(QQmlEngine &engine, const char *qml)
{
	QQmlComponent component(&engine);
	component.setData(qml, QUrl());
	std::unique_ptr<QObject> o(component.create());
	if (!o) std::printf("%s\n", qPrintable(component.errorString()));
	for (int i = 0; i < 5; i++) QCoreApplication::processEvents();
	return o;
}

QColor colour(const std::unique_ptr<QObject> &o, const char *name)
{
	return o ? o->property(name).value<QColor>() : QColor();
}
} // namespace

int main(int argc, char **argv)
{
	QQuickStyle::setStyle(QStringLiteral("Fusion"));
	QGuiApplication app(argc, argv);
	const QColor platformButton = QGuiApplication::palette().color(QPalette::Button);
	const QColor window(0x2B, 0x2B, 0x2B), button(0x3C, 0x3F, 0x41), field(0x1E, 0x1E, 0x1E);
	CHECK(platformButton != button);
	chimera::qt::applyTheme(QStringLiteral(
		R"({"WindowBackground": "#2B2B2B", "WindowText": "#E0E0E0", "ButtonBackground": "#3C3F41", "InputBackground": "#1E1E1E"})"));

	QQmlEngine engine;
	/* a window opened with the theme chosen: its controls and its menu */
	auto first = make(engine, kWindow);
	CHECK(first != nullptr);
	CHECK(colour(first, "windowColour") == window);
	CHECK(colour(first, "buttonColour") == button);
	CHECK(colour(first, "fieldColour") == field);
	CHECK(colour(first, "menuColour") == window);
	/* a window opened later, the same */
	auto second = make(engine, kWindow);
	CHECK(colour(second, "buttonColour") == button);
	/* a Lua form keeps what it set, and the rest of its colours are not the theme's */
	auto form = make(engine, kUnthemed);
	CHECK(colour(form, "buttonColour") == QColor(0x12, 0x34, 0x56));
	CHECK(colour(form, "windowColour") != window);
	/* another theme chosen while they are open */
	chimera::qt::applyTheme(QStringLiteral(R"({"WindowBackground": "#F0F0F0", "ButtonBackground": "#DDEEFF", "InputBackground": "#FFFFFF"})"));
	for (int i = 0; i < 5; i++) QCoreApplication::processEvents();
	CHECK(colour(first, "buttonColour") == QColor(0xDD, 0xEE, 0xFF));
	CHECK(colour(second, "fieldColour") == QColor(Qt::white));
	CHECK(colour(first, "menuColour") == QColor(0xF0, 0xF0, 0xF0));
	CHECK(colour(form, "buttonColour") == QColor(0x12, 0x34, 0x56));

	std::printf("%d checks, %d failed\n", checks, failed);
	return failed == 0 ? 0 : 1;
}
