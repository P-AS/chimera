/* Chimera's Qt window, a prototype (issue 142): the main window over the
 * session and piano roll view models. Everything a window does here goes
 * through the view models' registries, so a script could do the same.
 *
 *   chimera-qt [<package> <game> | <project> [<package>]] [--fill <frames>] [--play]
 *              [--measure <seconds>] [--scroll] [--screenshot <path>]
 *              [--lua <script>] [--describe]
 *
 * --measure runs for that long and prints what it measured, then quits:
 * frames the machine ran, pictures drawn, the window's frame intervals and,
 * with --scroll, how long each piano roll paint took while it scrolled. */

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QPalette>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif
#include <mutex>
#include <set>
#include <vector>

#include "chimera/vm/branches_vm.hpp"
#include "chimera/vm/config_store.hpp"
#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/lua_console_vm.hpp"
#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/ram_search_vm.hpp"
#include "chimera/vm/ram_watch_vm.hpp"
#include "chimera/vm/new_project_vm.hpp"
#include "chimera/vm/settings_vm.hpp"
#include "chimera/vm/session_vm.hpp"
#include "key_names.hpp"
#include "picture_item.hpp"
#include "piano_roll_item.hpp"
#include "qml_view_model.hpp"

namespace {

using namespace chimera;

/* Posts work into Qt's event loop: a view model's thread-hopping, from any thread. */
class QtDispatcher : public vm::Dispatcher
{
public:
	explicit QtDispatcher(QObject *context) : context_(context) {}
	void post(std::function<void()> fn) override
	{
		QMetaObject::invokeMethod(context_, std::move(fn), Qt::QueuedConnection);
	}

private:
	QObject *context_;
};

/* Keys pressed in the window go to the machine, by Chimera's key names; a
 * window that loses the keyboard lets go of every key it held. */
class KeyFilter : public QObject
{
public:
	explicit KeyFilter(vm::SessionViewModel &session) : session_(session) {}

protected:
	bool eventFilter(QObject *watched, QEvent *e) override
	{
		if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease)
		{
			auto *k = static_cast<QKeyEvent *>(e);
			if (!k->isAutoRepeat())
			{
				const QString name = qt::keyName(k->key(), k->modifiers());
				if (!name.isEmpty())
				{
					const bool down = e->type() == QEvent::KeyPress;
					session_.key(name.toStdString(), down);
					if (down) down_.insert(name);
					else down_.erase(name);
				}
			}
		}
		else if (e->type() == QEvent::FocusOut || e->type() == QEvent::WindowDeactivate)
		{
			for (const QString &name : down_) session_.key(name.toStdString(), false);
			down_.clear();
		}
		return QObject::eventFilter(watched, e);
	}

private:
	vm::SessionViewModel &session_;
	std::set<QString> down_;
};

#ifdef _WIN32
/* A crash names the DLL it happened in and where, on stderr: a prototype has
 * no crash report of its own yet. */
LONG WINAPI crashNote(EXCEPTION_POINTERS *e)
{
	void *at = e->ExceptionRecord->ExceptionAddress;
	HMODULE mod = nullptr;
	wchar_t name[MAX_PATH] = L"?";
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<LPCWSTR>(at), &mod))
		GetModuleFileNameW(mod, name, MAX_PATH);
	std::fprintf(stderr, "crash: exception %08lx at %ls+0x%llx (thread %lu)\n", e->ExceptionRecord->ExceptionCode, name,
		static_cast<unsigned long long>(static_cast<char *>(at) - reinterpret_cast<char *>(mod)), GetCurrentThreadId());
	std::fflush(stderr);
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

struct Spread
{
	double median = 0, p95 = 0, worst = 0;
};

Spread spread(std::vector<double> v)
{
	Spread s;
	if (v.empty()) return s;
	std::sort(v.begin(), v.end());
	s.median = v[v.size() / 2];
	s.p95 = v[std::min(v.size() - 1, v.size() * 95 / 100)];
	s.worst = v.back();
	return s;
}

} // namespace

int main(int argc, char **argv)
{
#ifdef _WIN32
	SetUnhandledExceptionFilter(crashNote);
#endif
	QGuiApplication app(argc, argv);
	/* not "Chimera": Qt keeps its caches under the application's name, and
	 * the prototype must not write into Chimera's own folder */
	QGuiApplication::setApplicationName(QStringLiteral("chimera-qt-prototype"));
	QQuickStyle::setStyle(QStringLiteral("Fusion"));

	QCommandLineParser args;
	args.addHelpOption();
	args.addPositionalArgument(QStringLiteral("package"), QStringLiteral("a core package to open"));
	args.addPositionalArgument(QStringLiteral("game"), QStringLiteral("the game file to open it with"));
	QCommandLineOption fillOpt(QStringLiteral("fill"), QStringLiteral("make the inputs this many frames long"), QStringLiteral("frames"));
	QCommandLineOption playOpt(QStringLiteral("play"), QStringLiteral("start running once open"));
	QCommandLineOption measureOpt(QStringLiteral("measure"), QStringLiteral("measure for this many seconds, print, quit"), QStringLiteral("seconds"));
	QCommandLineOption scrollOpt(QStringLiteral("scroll"), QStringLiteral("while measuring, scroll the piano roll all the time"));
	QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("when measuring ends, save the window as this picture"), QStringLiteral("path"));
	QCommandLineOption luaOpt(QStringLiteral("lua"), QStringLiteral("run a Lua script over the view models"), QStringLiteral("script"));
	QCommandLineOption schemeOpt(QStringLiteral("color-scheme"), QStringLiteral("light or dark, whatever the system says"), QStringLiteral("scheme"));
	QCommandLineOption showOpt(QStringLiteral("show"), QStringLiteral("open a tool window at the start: new-project, ram-search, ram-watch, settings, lua, branches, cores"), QStringLiteral("tool"));
	QCommandLineOption describeOpt(QStringLiteral("describe"), QStringLiteral("print every view model's properties and commands as JSON, and quit"));
	args.addOptions({fillOpt, playOpt, measureOpt, scrollOpt, shotOpt, luaOpt, describeOpt, schemeOpt, showOpt});
	args.process(app);
	if (args.isSet(schemeOpt))
	{
		const bool dark = args.value(schemeOpt) == QStringLiteral("dark");
		QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
		/* a platform with no dark theme of its own gets a plain dark palette */
		if (dark && QGuiApplication::palette().color(QPalette::Window).lightnessF() > 0.5)
		{
			QPalette p;
			const QColor window(0x2b, 0x2d, 0x30), base(0x1e, 0x1f, 0x22), text(0xe6, 0xe6, 0xe6);
			p.setColor(QPalette::Window, window);
			p.setColor(QPalette::WindowText, text);
			p.setColor(QPalette::Base, base);
			p.setColor(QPalette::AlternateBase, window);
			p.setColor(QPalette::Text, text);
			p.setColor(QPalette::Button, window);
			p.setColor(QPalette::ButtonText, text);
			p.setColor(QPalette::Highlight, QColor(0x3d, 0x8b, 0xff));
			p.setColor(QPalette::HighlightedText, Qt::white);
			p.setColor(QPalette::ToolTipBase, window);
			p.setColor(QPalette::ToolTipText, text);
			p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x8a));
			QGuiApplication::setPalette(p);
		}
	}

	QtDispatcher dispatcher(&app);
	vm::InputLog inputs;
	vm::SessionViewModel session(dispatcher, inputs);
	vm::PianoRollViewModel pianoRoll(dispatcher, inputs, session);
	vm::RamSearchViewModel ramSearch(dispatcher, session);
	vm::RamWatchViewModel ramWatch(dispatcher, session);
	vm::SettingsViewModel settings(dispatcher, session);
	vm::BranchesViewModel branches(dispatcher, inputs, session, pianoRoll);
	/* the cores and the firmware: beside the program, as Chimera keeps them,
	 * unless the environment names others */
	vm::CoresViewModel cores(dispatcher);
	if (cores.coresFolder.get().empty())
		cores.coresFolder.set((QCoreApplication::applicationDirPath() + QStringLiteral("/Cores")).toStdString());
	if (cores.firmwareFolder.get().empty())
		cores.firmwareFolder.set((QCoreApplication::applicationDirPath() + QStringLiteral("/Firmware")).toStdString());
	vm::NewProjectViewModel newProject(dispatcher, session, cores);
	session.setFinders([&cores](const std::string &n, const std::string &h, std::string &note) { return cores.packageFor(n, h, note); },
		[&cores] { return cores.firmwareFolders(); },
		[&cores](const std::string &extension) { return cores.coresFor(extension); });

	/* what the window remembers, in a file of its own (CHIMERA_QT_CONFIG for
	 * a test): folders the environment names are the environment's, and
	 * not remembered */
	const QString configPath = qEnvironmentVariableIsSet("CHIMERA_QT_CONFIG")
		? qEnvironmentVariable("CHIMERA_QT_CONFIG")
		: QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/settings.json");
	vm::ConfigStore config(configPath.toStdString());
	if (!qEnvironmentVariableIsSet("CHIMERA_CORES_DIR")) config.keep(cores, "coresFolder");
	if (!qEnvironmentVariableIsSet("CHIMERA_FIRMWARE_DIR")) config.keep(cores, "firmwareFolder");
	/* what a project can make again (its branches' states): the
	 * prototype's own cache, never Chimera's (CHIMERA_QT_CACHE for a test) */
	session.cacheFolder.set(qEnvironmentVariableIsSet("CHIMERA_QT_CACHE")
			? qEnvironmentVariable("CHIMERA_QT_CACHE").toStdString()
			: (QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/projects")).toStdString());
	config.keep(session, "autoRestore");
	config.keep(ramWatch, "showOnScreen");
	config.keep(session, "recentProjects");
	config.keep(pianoRoll, "followPlayback");
	if (!config.problem().empty()) session.status.set(config.problem());
	qt::QmlViewModel sessionQml(session);
	qt::QmlViewModel pianoRollQml(pianoRoll);
	qt::QmlViewModel ramSearchQml(ramSearch);
	qt::QmlViewModel ramWatchQml(ramWatch);
	qt::QmlViewModel newProjectQml(newProject);
	qt::QmlViewModel settingsQml(settings);
	qt::QmlViewModel branchesQml(branches);
	qt::QmlViewModel coresQml(cores);

	/* scripts reach the same view models, by the same names */
	vm::LuaBridge lua;
	/* what scripts print goes to the console window, and to stdout too */
	vm::LuaConsoleViewModel luaConsole(dispatcher, lua, [](const std::string &line) {
		std::printf("lua: %s\n", line.c_str());
		std::fflush(stdout);
	});
	qt::QmlViewModel luaConsoleQml(luaConsole);
	lua.add(session);
	lua.add(pianoRoll);
	lua.add(ramSearch);
	lua.add(ramWatch);
	lua.add(newProject);
	lua.add(settings);
	lua.add(luaConsole);
	lua.add(branches);
	lua.add(cores);
	if (args.isSet(describeOpt))
	{
		std::printf("%s", lua.describe().c_str());
		return 0;
	}
	/* after each report from the machine, every property updated - not on a
	 * change of frame number, which a seek away and back again never makes */
	vm::Connection luaFrames = session.screenChanged.connect([&lua] { lua.frameDone(); });

	qmlRegisterType<qt::PictureItem>("Chimera", 1, 0, "PictureItem");
	qmlRegisterType<qt::PianoRollItem>("Chimera", 1, 0, "PianoRollItem");

	QQmlApplicationEngine engine;
	engine.setInitialProperties({
		{QStringLiteral("session"), QVariant::fromValue<QObject *>(&sessionQml)},
		{QStringLiteral("pianoRoll"), QVariant::fromValue<QObject *>(&pianoRollQml)},
		{QStringLiteral("ramSearch"), QVariant::fromValue<QObject *>(&ramSearchQml)},
		{QStringLiteral("ramWatch"), QVariant::fromValue<QObject *>(&ramWatchQml)},
		{QStringLiteral("newProject"), QVariant::fromValue<QObject *>(&newProjectQml)},
		{QStringLiteral("settings"), QVariant::fromValue<QObject *>(&settingsQml)},
		{QStringLiteral("luaConsole"), QVariant::fromValue<QObject *>(&luaConsoleQml)},
		{QStringLiteral("branches"), QVariant::fromValue<QObject *>(&branchesQml)},
		{QStringLiteral("cores"), QVariant::fromValue<QObject *>(&coresQml)},
	});
	engine.load(QUrl(QStringLiteral("qrc:/chimera/Main.qml")));
	if (engine.rootObjects().isEmpty()) return 1;
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
	KeyFilter keys(session);
	window->installEventFilter(&keys);
	/* --show ram-search or settings: the tool windows by name */
	const QString showName = args.value(showOpt) == QStringLiteral("ram-search") ? QStringLiteral("ramSearchWindow")
		: args.value(showOpt) == QStringLiteral("ram-watch") ? QStringLiteral("ramWatchWindow")
		: args.value(showOpt) == QStringLiteral("new-project") ? QStringLiteral("newProjectWindow")
		: args.value(showOpt) == QStringLiteral("settings") ? QStringLiteral("settingsWindow")
		: args.value(showOpt) == QStringLiteral("lua") ? QStringLiteral("luaConsoleWindow")
		: args.value(showOpt) == QStringLiteral("branches") ? QStringLiteral("branchesWindow")
		: args.value(showOpt) == QStringLiteral("cores") ? QStringLiteral("coresWindow") : QString();
	if (!showName.isEmpty())
		if (auto *tool = window->findChild<QQuickWindow *>(showName)) tool->show();

	std::string error;
	if (args.isSet(luaOpt))
	{
		QFile file(args.value(luaOpt));
		if (!file.open(QIODevice::ReadOnly))
			std::printf("lua: could not read %s\n", qPrintable(args.value(luaOpt)));
		else
		{
			/* the script goes into the console too, as if opened and run there */
			luaConsole.path.set(args.value(luaOpt).toStdString());
			luaConsole.script.set(file.readAll().toStdString());
			luaConsole.run("run", {}, error);
		}
	}
	/* the movie is the machine's: it is made longer once one is open */
	vm::Connection fillWatch;
	if (args.isSet(fillOpt))
	{
		const int64_t frames = static_cast<int64_t>(args.value(fillOpt).toLongLong());
		fillWatch = session.opens.changed.connect([&pianoRoll, &fillWatch, frames] {
			std::string e;
			pianoRoll.run("fill", {frames}, e);
			fillWatch.disconnect();
		});
	}
	const QStringList positional = args.positionalArguments();
	if (positional.size() >= 2)
	{
		/* a project and its package, or a package and a game */
		if (positional[0].endsWith(QStringLiteral(".chimeraProject"), Qt::CaseInsensitive))
			session.run("openProject", {positional[0].toStdString(), positional[1].toStdString()}, error);
		else
			session.run("open", {positional[0].toStdString(), positional[1].toStdString()}, error);
	}
	else if (positional.size() == 1)
	{
		/* a project alone: its core is found among the installed ones */
		session.run("openProject", {positional[0].toStdString()}, error);
	}
	if (args.isSet(playOpt))
	{
		/* play once a machine is open: the one named here, or one a script
		 * opened */
		auto connection = std::make_shared<vm::Connection>();
		*connection = session.opens.changed.connect([&session, connection] {
			std::string e;
			session.run("play", {}, e);
			connection->disconnect();
		});
	}

	/* measuring */
	std::mutex swapM;
	std::vector<qint64> swaps;
	QElapsedTimer clock;
	clock.start();
	std::vector<double> paints;
	auto *roll = window->findChild<qt::PianoRollItem *>(QStringLiteral("pianoRoll"));
	auto *picture = window->findChild<qt::PictureItem *>(QStringLiteral("picture"));
	int64_t framesAtStart = -1;
	qint64 shownAtStart = 0;
	QElapsedTimer runClock;
	vm::Connection startWatch;
	int exitCode = 0;
	if (args.isSet(measureOpt))
	{
		const double seconds = args.value(measureOpt).toDouble();
		QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] {
			std::lock_guard<std::mutex> lock(swapM);
			swaps.push_back(clock.nsecsElapsed());
		}, Qt::DirectConnection);
		if (roll != nullptr)
			QObject::connect(roll, &qt::PianoRollItem::painted, roll, [&] { paints.push_back(roll->lastPaintMs()); });
		auto *scroller = new QTimer(&app);
		if (args.isSet(scrollOpt) && roll != nullptr)
		{
			pianoRoll.followPlayback.set(false);
			QObject::connect(scroller, &QTimer::timeout, &app, [&] {
				const int64_t count = std::max<int64_t>(1, inputs.size());
				pianoRoll.topFrame.set((pianoRoll.topFrame.get() + 997) % count);
			});
			scroller->start(16);
		}
		/* the clock starts when the machine does */
		startWatch = session.running.changed.connect([&] {
			if (session.running.get() && framesAtStart < 0)
			{
				framesAtStart = session.frame.get();
				shownAtStart = picture != nullptr ? picture->framesShown() : 0;
				runClock.start();
			}
		});
		QTimer::singleShot(static_cast<int>(seconds * 1000), &app, [&] {
			std::vector<double> intervals;
			{
				std::lock_guard<std::mutex> lock(swapM);
				for (size_t i = 1; i < swaps.size(); i++) intervals.push_back((swaps[i] - swaps[i - 1]) / 1.0e6);
			}
			const double ran = runClock.isValid() ? runClock.nsecsElapsed() / 1.0e9 : 0;
			const int64_t frames = framesAtStart >= 0 ? session.frame.get() - framesAtStart : 0;
			const qint64 shown = picture != nullptr ? picture->framesShown() - shownAtStart : 0;
			const Spread w = spread(intervals), p = spread(paints);
			std::printf("status: %s\n", session.status.get().c_str());
			std::printf("machine: %lld frames in %.2f s = %.2f fps (the machine's own rate is its vsync)\n",
				static_cast<long long>(frames), ran, ran > 0 ? frames / ran : 0.0);
			std::printf("pictures drawn: %lld\n", static_cast<long long>(shown));
			/* asked to play, a run that ran nothing failed (the window test) */
			if (args.isSet(playOpt) && frames <= 0) exitCode = 2;
			std::printf("window frames: %zu, interval ms median %.2f, 95th %.2f, worst %.2f\n",
				intervals.size() + (swaps.empty() ? 0 : 1), w.median, w.p95, w.worst);
			std::printf("piano roll: %lld rows, %zu paints, ms median %.3f, 95th %.3f, worst %.3f\n",
				static_cast<long long>(inputs.size()), paints.size(), p.median, p.p95, p.worst);
			if (args.isSet(shotOpt))
			{
				std::printf("screenshot: %s\n", window->grabWindow().save(args.value(shotOpt)) ? "saved" : "FAILED");
				/* every other window open, beside it: shot.png -> shot-ramSearchWindow.png */
				for (QWindow *w : QGuiApplication::topLevelWindows())
				{
					auto *q = qobject_cast<QQuickWindow *>(w);
					if (q == nullptr || q == window || !q->isVisible()) continue;
					QString path = args.value(shotOpt);
					path.insert(path.lastIndexOf('.'), QStringLiteral("-") + q->objectName());
					std::printf("screenshot: %s %s\n", qPrintable(q->objectName()), q->grabWindow().save(path) ? "saved" : "FAILED");
				}
			}
			std::fflush(stdout);
			app.exit(exitCode);
		});
	}

	return app.exec();
}
