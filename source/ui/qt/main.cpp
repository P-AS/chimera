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

#include <QSysInfo>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QClipboard>
#include <QImage>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QProcess>
#include <QFileInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPalette>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryFile>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif
#include <mutex>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "chimera/vm/branches_vm.hpp"
#include "chimera/vm/project_details_vm.hpp"
#include "chimera/vm/config_store.hpp"
#include "chimera/vm/help_vm.hpp"
#include "chimera/vm/batch_run_vm.hpp"
#include "chimera/vm/media_maker_vm.hpp"
#include "chimera/vm/cache_manager_vm.hpp"
#include "chimera/vm/customize_vm.hpp"
#include "chimera/vm/messages_vm.hpp"
#include "chimera/vm/display_vm.hpp"
#include "chimera/vm/paths_vm.hpp"
#include "chimera/vm/firmware_vm.hpp"
#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/lua_console_vm.hpp"
#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/ram_search_vm.hpp"
#include "chimera/vm/ram_watch_vm.hpp"
#include "chimera/vm/new_project_vm.hpp"
#include "chimera/vm/hex_editor_vm.hpp"
#include "chimera/vm/controls_vm.hpp"
#include "chimera/vm/hotkeys_vm.hpp"
#include "chimera/vm/themes_vm.hpp"
#include "theme_palette.hpp"
#include "chimera/vm/settings_vm.hpp"
#include "chimera/vm/session_vm.hpp"
#include "clipboard_relay.hpp"
#include "picture_files.hpp"
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

/* Keys pressed in the window go to the machine, by Chimera's key names, and
 * to the hotkeys, named with the modifiers held as Chimera writes them
 * ("Ctrl+Shift+V"); both, as Chimera does by default. A key typed into a
 * text field is the field's. A window that loses the keyboard lets go of
 * every key it held. */
class KeyFilter : public QObject
{
public:
	KeyFilter(vm::SessionViewModel &session, vm::HotkeysViewModel &hotkeys) : session_(session), hotkeys_(hotkeys) {}

protected:
	bool eventFilter(QObject *watched, QEvent *e) override
	{
		if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease)
		{
			auto *k = static_cast<QKeyEvent *>(e);
			auto *typing = qobject_cast<QQuickItem *>(QGuiApplication::focusObject());
			const bool down = e->type() == QEvent::KeyPress;
			if (down && typing != nullptr && typing->flags().testFlag(QQuickItem::ItemAcceptsInputMethod))
				return QObject::eventFilter(watched, e);
			if (!k->isAutoRepeat())
			{
				const QString name = qt::keyName(k->key(), k->modifiers());
				if (!name.isEmpty())
				{
					if (down)
					{
						/* the combination it was pressed with is the one let go;
						 * Key Priority says which of the two it goes to */
						const QString combo = qt::keyCombination(k->key(), k->modifiers());
						const auto route = vm::HotkeysViewModel::route(hotkeys_.keyPriority.get(),
							hotkeys_.boundTo(combo.toStdString()), session_.keyIsBound(name.toStdString()));
						down_[name] = {combo, route.hotkey, route.control};
						if (route.control) session_.key(name.toStdString(), true);
						if (route.hotkey) hotkeys_.key(combo.toStdString(), true);
					}
					else if (const auto it = down_.find(name); it != down_.end())
					{
						if (it->second.control) session_.key(name.toStdString(), false);
						if (it->second.hotkey) hotkeys_.key(it->second.combo.toStdString(), false);
						down_.erase(it);
					}
				}
			}
		}
		else if (e->type() == QEvent::FocusOut || e->type() == QEvent::WindowDeactivate)
		{
			for (const auto &[name, held] : down_)
			{
				if (held.control) session_.key(name.toStdString(), false);
				if (held.hotkey) hotkeys_.key(held.combo.toStdString(), false);
			}
			down_.clear();
		}
		return QObject::eventFilter(watched, e);
	}

private:
	vm::SessionViewModel &session_;
	vm::HotkeysViewModel &hotkeys_;
	struct Held
	{
		QString combo;
		bool hotkey = true, control = true;
	};
	std::map<QString, Held> down_;
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
	QCommandLineOption schemeOpt(QStringLiteral("color-scheme"), QStringLiteral("light or dark: Chimera's built-in Light or Dark theme for this run (not remembered)"), QStringLiteral("scheme"));
	QCommandLineOption showOpt(QStringLiteral("show"), QStringLiteral("open a tool window at the start: new-project, ram-search, ram-watch, hex, controls, themes, settings, lua, branches, undo-history, project-details, tastudio-settings, log, report, batch, media, cache, customize, messages, sound, display, budgets, paths, firmware, cores"), QStringLiteral("tool"));
	QCommandLineOption describeOpt(QStringLiteral("describe"), QStringLiteral("print every view model's properties and commands as JSON, and quit"));
	args.addOptions({fillOpt, playOpt, measureOpt, scrollOpt, shotOpt, luaOpt, describeOpt, schemeOpt, showOpt});
	args.process(app);
	QtDispatcher dispatcher(&app);
	vm::InputLog inputs;
	vm::SessionViewModel session(dispatcher, inputs);
	vm::PianoRollViewModel pianoRoll(dispatcher, inputs, session);
	vm::RamSearchViewModel ramSearch(dispatcher, session);
	vm::RamWatchViewModel ramWatch(dispatcher, session);
	vm::HexEditorViewModel hexEditor(dispatcher, session);
	vm::BranchesViewModel branches(dispatcher, inputs, session, pianoRoll);
	vm::ProjectDetailsViewModel projectDetails(dispatcher, session);
	vm::ControlsViewModel controls(dispatcher, session);
	vm::ThemesViewModel themes(dispatcher);
	vm::Connection themesWatch;
	/* Chimera's hotkeys, with its keys and in its order, for what this window
	 * has: each one the hotkeys' to bind. What a menu or a dialog does is the
	 * window's, and the relay hands it over; the rest is done here. */
	vm::HotkeysViewModel hotkeys(dispatcher);
	qt::HotkeyRelay relay;
	/* the main window, once it is made: the screen copied from it */
	QQuickWindow *mainWindow = nullptr;
	{
		auto run = [](vm::ViewModel &vm, std::string command, vm::Args args = {}) {
			std::string e;
			vm.run(command, args, e);
		};
		auto window = [&hotkeys, &relay](const std::string &group, const std::string &name, const std::string &keys) {
			hotkeys.add(group, name, keys, [&relay, name](bool down) {
				if (down) relay.press(name);
			});
		};
		hotkeys.add("General", "Frame Advance", "F", [&session](bool down) { session.holdFrameAdvance(down); });
		hotkeys.add("General", "Rewind", "Shift+R", [&session](bool down) { session.holdRewind(down); });
		hotkeys.add("General", "Pause", "Pause", [&session, run](bool down) {
			if (down) run(session, session.running.get() ? "pause" : "play");
		});
		hotkeys.add("General", "Fast Forward", "Tab", [&session](bool down) { session.holdFastForward(down); });
		/* held: as fast as it goes, nothing drawn or heard */
		hotkeys.add("General", "Turbo", "Shift+Tab", [&session](bool down) { session.holdTurbo(down); });
		hotkeys.add("General", "Toggle Throttle", "", [&session, run](bool down) {
			if (down) run(session, "toggleThrottle");
		});
		/* the picture on screen as a PNG, named as Chimera names one: the
		 * game, the date and time, and a number when that is taken */
		hotkeys.add("General", "Screenshot", "F12", [&session](bool down) {
			if (!down) return;
			vm::Picture::Frame f;
			if (!session.screen.takeIfNewer(UINT64_MAX, f) || f.width <= 0 || f.height <= 0)
			{
				session.status.set("No screenshot: there is no picture yet");
				return;
			}
			const QImage picture(reinterpret_cast<const uchar *>(f.pixels.data()), f.width, f.height, f.width * 4, QImage::Format_RGB32);
			QDir().mkpath(qt::PictureFiles::screenshotFolder());
			const QString base = qt::PictureFiles::screenshotBase(QString::fromStdString(session.gameName.get()));
			/* the bare name, unless a (0) is there; a bare one already there becomes (0) */
			QString path = base + QStringLiteral(" (0).png");
			if (!QFileInfo::exists(path))
			{
				if (QFileInfo::exists(base + QStringLiteral(".png"))) QFile::rename(base + QStringLiteral(".png"), path);
				else path = base + QStringLiteral(".png");
			}
			for (int n = 0; QFileInfo::exists(path); n++) path = base + QStringLiteral(" (%1).png").arg(n);
			if (picture.save(path, "PNG")) session.status.set(QFileInfo(path).fileName().toStdString() + " saved.");
			else session.status.set("No screenshot: " + path.toStdString() + " could not be written");
		});
		/* the picture as the machine made it, or the window as it is, to the
		 * clipboard; Chimera's Ctrl+C for the first is the piano roll's Copy
		 * here, so it has no key until one is given */
		hotkeys.add("General", "Screen Raw to Clipboard", "", [&session](bool down) {
			vm::Picture::Frame f;
			if (!down || !session.screen.takeIfNewer(UINT64_MAX, f) || f.width <= 0 || f.height <= 0) return;
			QGuiApplication::clipboard()->setImage(
				QImage(reinterpret_cast<const uchar *>(f.pixels.data()), f.width, f.height, f.width * 4, QImage::Format_RGB32).copy());
			session.status.set("The picture is on the clipboard");
		});
		hotkeys.add("General", "Screen Client to Clipboard", "Ctrl+Shift+C", [&session, &mainWindow](bool down) {
			if (!down || mainWindow == nullptr) return;
			QGuiApplication::clipboard()->setImage(mainWindow->grabWindow());
			session.status.set("The window is on the clipboard");
		});
		window("General", "Toggle Menu", "");
		window("General", "Toggle Stay on Top", "");
		window("General", "Full Screen", "Alt+Enter");
		hotkeys.add("General", "Autofire", "", [&session](bool down) { session.armAutofire(down); });
		hotkeys.add("General", "Autohold", "", [&session](bool down) { session.armAutohold(down); });
		hotkeys.add("General", "Clear Autohold", "", [&session, run](bool down) {
			if (down) run(session, "clearAutohold");
		});
		auto flip = [](vm::Property<bool> &p) { p.set(!p.get()); };
		hotkeys.add("General", "Display FPS", "", [&session, flip](bool down) {
			if (down) flip(session.showFps);
		});
		hotkeys.add("General", "Frame Counter", "", [&session, flip](bool down) {
			if (down) flip(session.showFrameCounter);
		});
		hotkeys.add("General", "Lag Counter", "", [&session, flip](bool down) {
			if (down) flip(session.showLagCounter);
		});
		hotkeys.add("General", "Game Time", "", [&session, flip](bool down) {
			if (down && session.hasGameTimer.get()) flip(session.showGameTime);
		});
		hotkeys.add("General", "Input Display", "", [&session, flip](bool down) {
			if (down) flip(session.showInput);
		});
		/* Chimera's two names for one switch: a gamepad heard behind other windows */
		auto background = [&session, flip](bool down) {
			if (!down) return;
			flip(session.acceptBackgroundInput);
			session.status.set(std::string("Background Input ") + (session.acceptBackgroundInput.get() ? "enabled" : "disabled"));
		};
		hotkeys.add("General", "Toggle BG Input", "", background);
		window("General", "Open ROM", "Ctrl+O");
		/* the window's: a project with changes is asked about first */
		window("General", "Close ROM", "Ctrl+W");
		window("General", "Load Last ROM", "");
		hotkeys.add("General", "Reboot Core", "Ctrl+R", [&session, run](bool down) {
			if (down) run(session, "restart");
		});
		hotkeys.add("General", "Volume Up", "", [&session, run](bool down) {
			if (down) run(session, "volumeUp");
		});
		hotkeys.add("General", "Volume Down", "", [&session, run](bool down) {
			if (down) run(session, "volumeDown");
		});
		/* the window a size up or down, 1x to 10x the picture */
		window("General", "Larger Window", "Alt+Up");
		window("General", "Smaller Window", "Alt+Down");
		hotkeys.add("General", "Increase Speed", "Equals", [&session, run](bool down) {
			if (down) run(session, "speedUp");
		});
		hotkeys.add("General", "Decrease Speed", "Minus", [&session, run](bool down) {
			if (down) run(session, "speedDown");
		});
		hotkeys.add("General", "Reset Speed", "Shift+Equals", [&session, run](bool down) {
			if (down) run(session, "speedReset");
		});
		hotkeys.add("General", "Toggle Sound", "", [&session, run](bool down) {
			if (down) run(session, "toggleSound");
		});
		hotkeys.add("General", "Toggle Skip Lag Frame", "", [&session, flip](bool down) {
			if (!down) return;
			flip(session.skipLagFrames);
			session.status.set(std::string("Skip Lag Frames toggled ") + (session.skipLagFrames.get() ? "On" : "Off"));
		});
		hotkeys.add("General", "Toggle Key Priority", "", [&hotkeys, run](bool down) {
			if (down) run(hotkeys, "cycleKeyPriority");
		});
		/* paused, a frame; running, a pause */
		hotkeys.add("General", "Frame Inch", "", [&session, run](bool down) {
			if (down) run(session, session.running.get() ? "pause" : "frameAdvance");
		});
		hotkeys.add("General", "Toggle Messages", "", [&session, flip](bool down) {
			if (down) flip(session.showMessages);
		});
		hotkeys.add("General", "Toggle Display Nothing", "", [&session, flip](bool down) {
			if (down) flip(session.displayNothing);
		});
		hotkeys.add("General", "Accept Background Input", "", background);
		window("General", "Exit Program", "");
		window("Tools", "RAM Watch", "");
		window("Tools", "RAM Search", "");
		window("Tools", "Hex Editor", "");
		window("Tools", "Lua Console", "");
		hotkeys.add("RAM Search", "New Search", "", [&ramSearch, run](bool down) {
			if (down) run(ramSearch, "start");
		});
		hotkeys.add("RAM Search", "Do Search", "", [&ramSearch, run](bool down) {
			if (down) run(ramSearch, "search");
		});
		/* what a search compares with (five ways) and how (seven), round */
		auto cycle = [](vm::Property<int64_t> &p, int64_t by, int64_t ways) { p.set(((p.get() + by) % ways + ways) % ways); };
		hotkeys.add("RAM Search", "Previous Compare To", "", [&ramSearch, cycle](bool down) {
			if (down) cycle(ramSearch.compare, -1, 5);
		});
		hotkeys.add("RAM Search", "Next Compare To", "", [&ramSearch, cycle](bool down) {
			if (down) cycle(ramSearch.compare, 1, 5);
		});
		hotkeys.add("RAM Search", "Previous Operator", "", [&ramSearch, cycle](bool down) {
			if (down) cycle(ramSearch.op, -1, 7);
		});
		hotkeys.add("RAM Search", "Next Operator", "", [&ramSearch, cycle](bool down) {
			if (down) cycle(ramSearch.op, 1, 7);
		});
		hotkeys.add("TAStudio", "Add Branch", "Alt+Insert", [&branches, run](bool down) {
			if (down) run(branches, "create");
		});
		hotkeys.add("TAStudio", "Delete Branch", "Alt+Delete", [&branches, run](bool down) {
			if (down && branches.selected.get() >= 0) run(branches, "remove", {branches.selected.get()});
		});
		for (int slot = 1; slot <= 10; slot++)
		{
			const std::string n = std::to_string(slot), key = "F" + n;
			hotkeys.add("TAStudio", "Save Branch " + n, "Shift+" + key, [&branches, run, slot](bool down) {
				if (down) run(branches, "saveSlot", {int64_t{slot - 1}});
			});
			hotkeys.add("TAStudio", "Load Branch " + n, key, [&branches, run, slot](bool down) {
				if (down) run(branches, "load", {int64_t{slot - 1}});
			});
		}
		hotkeys.add("TAStudio", "Show Cursor", "", [&pianoRoll](bool down) {
			if (down) pianoRoll.topFrame.set(std::max<int64_t>(0, pianoRoll.currentFrame.get() - pianoRoll.visibleFrames.get() / 2));
		});
		hotkeys.add("TAStudio", "Select Current Frame", "", [&pianoRoll, run](bool down) {
			if (down) run(pianoRoll, "select", {pianoRoll.currentFrame.get(), pianoRoll.currentFrame.get()});
		});
		hotkeys.add("TAStudio", "Seek To Selected Frame", "", [&pianoRoll, &session, run](bool down) {
			if (down && pianoRoll.selectFirst.get() >= 0) run(session, "seek", {pianoRoll.selectFirst.get()});
		});
		window("TAStudio", "Seek To...", "Ctrl+G");
		hotkeys.add("TAStudio", "Toggle Follow Cursor", "Shift+F", [&pianoRoll](bool down) {
			if (down) pianoRoll.followPlayback.set(!pianoRoll.followPlayback.get());
		});
		hotkeys.add("TAStudio", "Toggle Auto-Restore", "Alt+R", [&session](bool down) {
			if (down) session.autoRestore.set(!session.autoRestore.get());
		});
		hotkeys.add("TAStudio", "Seek To Green Arrow", "R", [&session, run](bool down) {
			if (down) run(session, "seekToRestore");
		});
		hotkeys.add("TAStudio", "Cycle Greenzone", "", [&session, run](bool down) {
			if (down) run(session, "cycleGreenzone");
		});
		window("TAStudio", "Undo", "Ctrl+Z");
		window("TAStudio", "Redo", "Ctrl+Y");
		auto seekMarker = [&pianoRoll, &session, run](const char *which) {
			std::string e;
			const auto to = pianoRoll.run(which, {pianoRoll.currentFrame.get()}, e);
			int64_t frame = -1;
			if (to && vm::fromValue(*to, frame) && frame >= 0) run(session, "seek", {frame});
		};
		hotkeys.add("TAStudio", "Seek To Prev Marker", "Shift+PageUp", [seekMarker](bool down) {
			if (down) seekMarker("previousMarker");
		});
		hotkeys.add("TAStudio", "Seek To Next Marker", "Shift+PageDown", [seekMarker](bool down) {
			if (down) seekMarker("nextMarker");
		});
		hotkeys.add("TAStudio", "Set Marker", "M", [&pianoRoll, run](bool down) {
			if (down) run(pianoRoll, "addMarker", {pianoRoll.currentFrame.get(), std::string()});
		});
		hotkeys.add("TAStudio", "Delete Marker", "Ctrl+M", [&pianoRoll, run](bool down) {
			if (down) run(pianoRoll, "removeMarker", {pianoRoll.currentFrame.get()});
		});
		window("TAStudio", "Sel. bet. Markers", "Ctrl+A");
		window("TAStudio", "Select All", "Ctrl+Shift+A");
		hotkeys.add("TAStudio", "Reselect Clip.", "Ctrl+B", [&pianoRoll, run](bool down) {
			if (down) run(pianoRoll, "reselectClipboard");
		});
		window("TAStudio", "Clear Frames", "Delete");
		window("TAStudio", "Delete Frames", "Ctrl+Delete");
		window("TAStudio", "Insert Frame", "Insert");
		window("TAStudio", "Insert # Frames", "Shift+Insert");
		window("TAStudio", "Clone Frames", "Ctrl+Insert");
		window("TAStudio", "Clone # Times", "Ctrl+Shift+Insert");
		window("TAStudio", "Paste Insert", "Ctrl+Shift+V");
		/* the picked analog cell's value; nothing when none is picked */
		for (const auto &[name, keys, how] : std::initializer_list<std::tuple<const char *, const char *, const char *>>{
				 {"Analog Increment", "Up", "up"}, {"Analog Decrement", "Down", "down"}, {"Analog Incr. by 10", "Shift+Up", "up10"},
				 {"Analog Decr. by 10", "Shift+Down", "down10"}, {"Analog Maximum", "Right", "max"}, {"Analog Minimum", "Left", "min"}})
		{
			const std::string move = how;
			hotkeys.add("TAStudio", name, keys, [&pianoRoll, run, move](bool down) {
				if (down) run(pianoRoll, "nudgeAxis", {move});
			});
		}
	}
	vm::SettingsViewModel settings(dispatcher, session);
	vm::HelpViewModel help(dispatcher, session, settings);
	/* the system, for a bug report: as Qt names it */
	help.osName.set((QSysInfo::prettyProductName() + QStringLiteral(" (") + QSysInfo::currentCpuArchitecture() + QStringLiteral(")")).toStdString());
	/* the cores and the firmware: beside the program, as Chimera keeps them,
	 * unless the environment names others */
	vm::CoresViewModel cores(dispatcher);
	if (cores.coresFolder.get().empty())
		cores.coresFolder.set((QCoreApplication::applicationDirPath() + QStringLiteral("/Cores")).toStdString());
	if (cores.firmwareFolder.get().empty())
		cores.firmwareFolder.set((QCoreApplication::applicationDirPath() + QStringLiteral("/Firmware")).toStdString());
	vm::NewProjectViewModel newProject(dispatcher, session, cores);
	/* the core's suggested settings for a game, asked as an import is */
	newProject.setSuggestRunner([&app](const std::string &package, const std::string &game,
									const std::vector<std::pair<std::string, std::string>> &files, const std::string &slots,
									const std::string &settings, std::function<void(std::string, std::string)> done) {
#ifdef _WIN32
		const QString exe = QStringLiteral("chimera-run.exe");
#else
		const QString exe = QStringLiteral("chimera-run");
#endif
		QString runner = qEnvironmentVariable("CHIMERA_RUN");
		for (const QString &dir : {QCoreApplication::applicationDirPath(), QCoreApplication::applicationDirPath() + QStringLiteral("/../..")})
			if (runner.isEmpty() && QFileInfo::exists(dir + QStringLiteral("/") + exe)) runner = dir + QStringLiteral("/") + exe;
		if (runner.isEmpty())
		{
			done({}, "chimera-run is not beside the program");
			return;
		}
		auto *proc = new QProcess(&app);
		/* the slot map is mounted as a file: one that lives as long as the process */
		auto *slotMap = new QTemporaryFile(proc);
		QStringList args{QString::fromStdString(package), QString::fromStdString(game), QStringLiteral("--suggest"),
			QStringLiteral("--settings"), QString::fromStdString(settings)};
		if (slotMap->open())
		{
			slotMap->write(QByteArray::fromStdString(slots));
			slotMap->flush();
			args << QStringLiteral("--mount") << QStringLiteral("slots=") + slotMap->fileName();
		}
		for (const auto &[name, path] : files)
			args << QStringLiteral("--mount") << QString::fromStdString(name + "=" + path);
		QObject::connect(proc, &QProcess::finished, proc, [proc, done](int code, QProcess::ExitStatus) {
			QByteArray answer;
			for (const QByteArray &l : proc->readAllStandardOutput().split('\n'))
				if (l.trimmed().startsWith('{')) answer = l.trimmed();
			const QByteArray err = proc->readAllStandardError().trimmed();
			proc->deleteLater();
			if (code == 0 && !answer.isEmpty()) done(answer.toStdString(), {});
			else done({}, err.isEmpty() ? std::string("the core gave none") : err.split('\n').last().toStdString());
		});
		proc->start(runner, args);
	});
	/* the headless runner, for what runs a machine in a process of its own:
	 * chimera-run, beside the program or, in the build tree, where it is
	 * built (CHIMERA_RUN names another) */
	auto startRunner = [&app](std::vector<std::string> args, std::function<void(int, std::string, std::string)> done) {
#ifdef _WIN32
		const QString exe = QStringLiteral("chimera-run.exe");
#else
		const QString exe = QStringLiteral("chimera-run");
#endif
		QString runner = qEnvironmentVariable("CHIMERA_RUN");
		for (const QString &dir : {QCoreApplication::applicationDirPath(), QCoreApplication::applicationDirPath() + QStringLiteral("/../..")})
			if (runner.isEmpty() && QFileInfo::exists(dir + QStringLiteral("/") + exe)) runner = dir + QStringLiteral("/") + exe;
		if (runner.isEmpty())
		{
			done(-1, {}, "chimera-run is not beside the program");
			return;
		}
		QStringList list;
		for (const std::string &a : args) list << QString::fromStdString(a);
		auto *proc = new QProcess(&app);
		QObject::connect(proc, &QProcess::finished, proc, [proc, done](int code, QProcess::ExitStatus) {
			const std::string out = proc->readAllStandardOutput().toStdString(), err = proc->readAllStandardError().toStdString();
			proc->deleteLater();
			done(code, out, err);
		});
		QObject::connect(proc, &QProcess::errorOccurred, proc, [proc, done](QProcess::ProcessError e) {
			if (e != QProcess::FailedToStart) return;
			proc->deleteLater();
			done(-1, {}, "chimera-run could not be started");
		});
		proc->start(runner, list);
	};
	vm::BatchRunViewModel batchRun(dispatcher, startRunner, [&cores](const std::string &extension) { return cores.coresFor(extension); });
	vm::MediaMakerViewModel mediaMaker(dispatcher, [&cores] {
		std::vector<vm::MediaMakerViewModel::Recipe> recipes;
		for (const auto &r : cores.mediaRecipes()) recipes.push_back({r.core, r.label, r.json});
		return recipes;
	});
	batchRun.workFolder.set(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("chimera-batch")).toStdString());
	/* a movie import asks the core in a process of its own (one machine to a
	 * process), as Chimera does: chimera-run, beside the program or, in the
	 * build tree, where it is built (CHIMERA_RUN names another) */
	newProject.setImportRunner([&app](const std::string &package, const std::string &movie,
								   const std::vector<std::pair<std::string, std::string>> &mounts, const std::string &settings,
								   std::function<void(std::string, std::string)> done) {
#ifdef _WIN32
		const QString exe = QStringLiteral("chimera-run.exe");
#else
		const QString exe = QStringLiteral("chimera-run");
#endif
		QString runner = qEnvironmentVariable("CHIMERA_RUN");
		for (const QString &dir : {QCoreApplication::applicationDirPath(), QCoreApplication::applicationDirPath() + QStringLiteral("/../..")})
			if (runner.isEmpty() && QFileInfo::exists(dir + QStringLiteral("/") + exe)) runner = dir + QStringLiteral("/") + exe;
		if (runner.isEmpty())
		{
			done({}, "chimera-run is not beside the program");
			return;
		}
		QStringList args{QString::fromStdString(package), QString::fromStdString(movie), QStringLiteral("--import-movie"),
			QStringLiteral("--settings"), QString::fromStdString(settings)};
		for (const auto &[name, path] : mounts) args << QStringLiteral("--mount") << QString::fromStdString(name + "=" + path);
		auto *proc = new QProcess(&app);
		QObject::connect(proc, &QProcess::finished, proc, [proc, done](int code, QProcess::ExitStatus) {
			/* the answer is the last line that is a JSON object; a core may print */
			const QList<QByteArray> lines = proc->readAllStandardOutput().split('\n');
			QByteArray answer;
			for (const QByteArray &l : lines)
				if (l.trimmed().startsWith('{')) answer = l.trimmed();
			const QByteArray err = proc->readAllStandardError().trimmed();
			proc->deleteLater();
			if (code == 0 && !answer.isEmpty()) done(answer.toStdString(), {});
			else done({}, err.isEmpty() ? std::string("chimera-run gave no answer") : err.split('\n').last().toStdString());
		});
		QObject::connect(proc, &QProcess::errorOccurred, proc, [proc, done](QProcess::ProcessError e) {
			if (e != QProcess::FailedToStart) return;
			proc->deleteLater();
			done({}, "chimera-run could not be started");
		});
		proc->start(runner, args);
	});
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
	config.keep(cores, "chosenFirmware");
	config.keep(newProject, "kind");
	/* what a project can make again (its branches' states): the
	 * prototype's own cache, never Chimera's (CHIMERA_QT_CACHE for a test) */
	session.cacheFolder.set(qEnvironmentVariableIsSet("CHIMERA_QT_CACHE")
			? qEnvironmentVariable("CHIMERA_QT_CACHE").toStdString()
			: (QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/projects")).toStdString());
	/* unsaved work kept against a crash: beside the caches, never in one, so
	 * removing a cache cannot take it (CHIMERA_QT_RECOVERY for a test) */
	session.recoveryFolder.set(qEnvironmentVariableIsSet("CHIMERA_QT_RECOVERY")
			? qEnvironmentVariable("CHIMERA_QT_RECOVERY").toStdString()
			: (QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/Recovery")).toStdString());
	config.keep(session, "autoRestore");
	config.keep(session, "speedPercent");
	config.keep(session, "unthrottled");
	config.keep(session, "soundOn");
	config.keep(session, "volume");
	config.keep(session, "showFps");
	config.keep(session, "showFrameCounter");
	config.keep(session, "showLagCounter");
	config.keep(session, "showInput");
	config.keep(session, "showAverageFps");
	config.keep(session, "showRerecords");
	config.keep(session, "showMessages");
	config.keep(session, "skipLagFrames");
	config.keep(session, "fastForwardPercent");
	config.keep(session, "greenzoneBudgetMb");
	config.keep(session, "greenzoneNearStride");
	config.keep(session, "pictureSettleFrames");
	for (const char *name : {"soundDevice", "soundBufferMs", "soundNormalOn", "soundFastOn", "fastVolume", "muteFrameAdvance"})
		config.keep(session, name);
	config.keep(session, "frameSkip");
	config.keep(session, "autoMinimizeSkipping");
	config.keep(session, "displayNothing");
	config.keep(session, "acceptBackgroundInput");
	/* the window in front or not: a gamepad is heard behind other windows only when asked */
	session.windowActive.set(QGuiApplication::applicationState() == Qt::ApplicationActive);
	QObject::connect(qApp, &QGuiApplication::applicationStateChanged, [&session](Qt::ApplicationState state) {
		session.windowActive.set(state == Qt::ApplicationActive);
	});
	config.keep(session, "showStatusBar");
	config.keep(session, "showGameTime");
	config.keep(batchRun, "frames");
	config.keep(session, "autofireOn");
	config.keep(session, "autofireOff");
	config.keep(session, "autofireSkipsLag");
	/* backups where Chimera puts them, beside the program, unless kept elsewhere */
	/* Config > Paths: the folders, relative to the program's own at first;
	 * the backups and the firmware follow them */
	vm::PathsViewModel paths(dispatcher);
	paths.programFolder.set(QCoreApplication::applicationDirPath().toStdString());
	for (const char *name : {"base", "changed", "recentForRoms", "lastRomFolder"}) config.keep(paths, name);
	auto followPaths = [&paths, &session, &cores] {
		session.backupFolder.set(paths.resolve("Movie backups"));
		if (!qEnvironmentVariableIsSet("CHIMERA_FIRMWARE_DIR") && cores.firmwareFolder.get() != paths.resolve("Firmware"))
			cores.firmwareFolder.set(paths.resolve("Firmware"));
	};
	followPaths();
	vm::Connection pathsFollowed = paths.moved.connect(followPaths);
	/* the firmware folder chosen elsewhere (the Cores window) is the Paths' too */
	vm::Connection firmwareFollowed = cores.firmwareFolder.changed.connect([&paths, &cores] {
		if (qEnvironmentVariableIsSet("CHIMERA_FIRMWARE_DIR") || cores.firmwareFolder.get() == paths.resolve("Firmware")) return;
		std::string e;
		paths.run("setPath", {std::string("Firmware"), cores.firmwareFolder.get()}, e);
	});
	qt::PictureFiles::folderFor() = [&paths] { return QString::fromStdString(paths.resolve("Screenshots")); };
	qt::QmlViewModel pathsQml(paths);
	vm::FirmwareViewModel firmware(dispatcher, cores);
	qt::QmlViewModel firmwareQml(firmware);
	config.keep(projectDetails, "defaultAuthor");
	config.keep(pianoRoll, "maxUndoSteps");
	config.keep(pianoRoll, "copyFrameNumbers");
	config.keep(pianoRoll, "followScroll");
	config.keep(pianoRoll, "followAlways");
	config.keep(pianoRoll, "wheelRows");
	config.keep(pianoRoll, "editHiddenColumns");
	config.keep(pianoRoll, "patternMode");
	config.keep(pianoRoll, "patternKind");
	config.keep(pianoRoll, "denoteStatesColor");
	config.keep(pianoRoll, "denoteStatesIcons");
	config.keep(pianoRoll, "denoteMarkersColor");
	config.keep(pianoRoll, "denoteMarkersIcons");
	config.keep(pianoRoll, "rollColors");
	config.keep(pianoRoll, "rollFont");
	config.keep(pianoRoll, "recentMacros");
	config.keep(pianoRoll, "markersKeepStates");
	config.keep(pianoRoll, "bindMarkers");
	config.keep(session, "pauseAtEnd");
	config.keep(session, "rewindStep");
	config.keep(session, "rewindStepFast");
	config.keep(session, "autosaveSeconds");
	config.keep(session, "autosaveAsBackup");
	config.keep(session, "backupOnSave");
	config.keep(branches, "loadOnDoubleClick");
	config.keep(branches, "oldControlScheme");
	config.keep(branches, "previewSide");
	config.keep(projectDetails, "showSubtitles");
	config.keep(session, "greenzoneEvery");
	config.keep(ramWatch, "showOnScreen");
	config.keep(controls, "saved");
	/* the themes: Chimera's built-in two, and the folder beside the program
	 * (CHIMERA_THEMES_DIR for a test); --color-scheme picks Light or Dark for
	 * this run only */
	for (const QString &file : {QStringLiteral(":/chimera/themes/light.json"), QStringLiteral(":/chimera/themes/dark.json")})
	{
		QFile f(file);
		std::string why;
		if (f.open(QIODevice::ReadOnly)) themes.addBuiltIn(f.readAll().toStdString(), why);
	}
	/* the Paths window's Themes folder (CHIMERA_THEMES_DIR for a test) */
	auto themesFollow = [&themes, &paths] {
		themes.folder.set(qEnvironmentVariableIsSet("CHIMERA_THEMES_DIR") ? qEnvironmentVariable("CHIMERA_THEMES_DIR").toStdString()
			: paths.resolve("Themes"));
	};
	themesFollow();
	vm::Connection themesFollowPaths = paths.moved.connect(themesFollow);
	if (args.isSet(schemeOpt))
		themes.current.set(args.value(schemeOpt) == QStringLiteral("light") ? "Light" : "Dark");
	else
		config.keep(themes, "current");
	auto applyTheme = [&themes] { qt::applyTheme(QString::fromStdString(themes.colors.get())); };
	themesWatch = themes.colors.changed.connect(applyTheme);
	applyTheme();
	config.keep(hotkeys, "saved");
	config.keep(hotkeys, "keyPriority");
	config.keep(hotkeys, "noMixedPriority");
	config.keep(session, "recentProjects");
	config.keep(pianoRoll, "followPlayback");
	if (!config.problem().empty()) session.status.set(config.problem());
	qt::QmlViewModel sessionQml(session);
	qt::QmlViewModel pianoRollQml(pianoRoll);
	qt::QmlViewModel ramSearchQml(ramSearch);
	qt::QmlViewModel ramWatchQml(ramWatch);
	qt::QmlViewModel newProjectQml(newProject);
	qt::QmlViewModel hexEditorQml(hexEditor);
	qt::QmlViewModel controlsQml(controls);
	qt::QmlViewModel themesQml(themes);
	qt::QmlViewModel hotkeysQml(hotkeys);
	qt::KeyNamer keyNamer;
	qt::ThemeColors themeColors;
	qt::QmlViewModel settingsQml(settings);
	qt::QmlViewModel helpQml(help);
	qt::QmlViewModel batchRunQml(batchRun);
	qt::QmlViewModel mediaMakerQml(mediaMaker);
	vm::CacheManagerViewModel cacheManager(dispatcher);
	/* the session's cache folder, the one it lists */
	cacheManager.cacheFolder.set(session.cacheFolder.get());
	vm::Connection cacheFolderFollows = session.cacheFolder.changed.connect([&] { cacheManager.cacheFolder.set(session.cacheFolder.get()); });
	cacheManager.recoveryFolder.set(session.recoveryFolder.get());
	vm::Connection recoveryFolderFollows = session.recoveryFolder.changed.connect([&] { cacheManager.recoveryFolder.set(session.recoveryFolder.get()); });
	config.keep(cacheManager, "autoClean");
	config.keep(cacheManager, "limitGb");
	config.keep(cacheManager, "floorGb");
	/* Chimera's auto-clean: at the start, and when a project closes - never
	 * while one is worked on, and never the one just closed */
	auto autoCleanSaying = [&cacheManager, &session](const std::string &spare) {
		std::string e;
		const auto said = cacheManager.run("runAutoClean", {spare}, e);
		std::string text;
		if (said && vm::fromValue(*said, text) && !text.empty()) session.status.set(text);
	};
	autoCleanSaying("");
	std::string lastProjectId = session.projectId.get();
	vm::Connection cacheInUseFollows = session.projectId.changed.connect([&] {
		cacheManager.inUse.set(session.projectId.get());
		const std::string closed = std::exchange(lastProjectId, session.projectId.get());
		if (!closed.empty()) autoCleanSaying(closed);
	});
	qt::QmlViewModel cacheManagerQml(cacheManager);
	vm::CustomizeViewModel customize(dispatcher, session);
	for (const char *name : {"startPaused", "startFullscreen", "pauseWhenMenuActivated", "runInBackground", "neverAskSave", "luaDuringTurbo"})
		config.keep(customize, name);
	qt::QmlViewModel customizeQml(customize);
	vm::MessagesViewModel messages(dispatcher);
	for (const char *name : {"positions", "messagesColor", "alertColor", "lastInputColor", "movieInputColor", "messageSeconds", "stackMessages"})
		config.keep(messages, name);
	qt::QmlViewModel messagesQml(messages);
	vm::DisplayViewModel display(dispatcher);
	for (const char *name : {"aspectMode", "customWidth", "customHeight", "ratioX", "ratioY", "letterbox", "integerScale", "cropLeft",
			 "cropTop", "cropRight", "cropBottom", "finalFilter", "saveWindowPosition", "windowGeometry"})
		config.keep(display, name);
	qt::QmlViewModel displayQml(display);
	qt::QmlViewModel branchesQml(branches);
	qt::QmlViewModel projectDetailsQml(projectDetails);
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
	lua.add(hexEditor);
	lua.add(controls);
	lua.add(themes);
	lua.add(hotkeys);
	lua.add(settings);
	lua.add(luaConsole);
	lua.add(branches);
	lua.add(projectDetails);
	lua.add(cores);
	lua.add(help);
	lua.add(batchRun);
	lua.add(mediaMaker);
	lua.add(cacheManager);
	lua.add(customize);
	lua.add(messages);
	lua.add(display);
	lua.add(paths);
	lua.add(firmware);
	if (args.isSet(describeOpt))
	{
		std::printf("%s", lua.describe().c_str());
		return 0;
	}
	/* after each report from the machine, every property updated - not on a
	 * change of frame number, which a seek away and back again never makes */
	/* scripts while turbo is held only when asked (Customize) */
	vm::Connection luaFrames = session.screenChanged.connect([&lua, &customize] {
		if (customize.luaRunsNow()) lua.frameDone();
	});

	qmlRegisterType<qt::PictureItem>("Chimera", 1, 0, "PictureItem");
	qmlRegisterType<qt::PianoRollItem>("Chimera", 1, 0, "PianoRollItem");

	qt::ClipboardRelay clipboard;
	qt::PictureFiles pictureFiles(session.screen);
	pictureFiles.say = [&session](const std::string &text) { session.status.set(text); };
	QQmlApplicationEngine engine;
	engine.setInitialProperties({
		{QStringLiteral("session"), QVariant::fromValue<QObject *>(&sessionQml)},
		{QStringLiteral("pianoRoll"), QVariant::fromValue<QObject *>(&pianoRollQml)},
		{QStringLiteral("ramSearch"), QVariant::fromValue<QObject *>(&ramSearchQml)},
		{QStringLiteral("ramWatch"), QVariant::fromValue<QObject *>(&ramWatchQml)},
		{QStringLiteral("newProject"), QVariant::fromValue<QObject *>(&newProjectQml)},
		{QStringLiteral("hexEditor"), QVariant::fromValue<QObject *>(&hexEditorQml)},
		{QStringLiteral("controls"), QVariant::fromValue<QObject *>(&controlsQml)},
		{QStringLiteral("themes"), QVariant::fromValue<QObject *>(&themesQml)},
		{QStringLiteral("themeColors"), QVariant::fromValue<QObject *>(&themeColors)},
		{QStringLiteral("hotkeys"), QVariant::fromValue<QObject *>(&hotkeysQml)},
		{QStringLiteral("keyNamer"), QVariant::fromValue<QObject *>(&keyNamer)},
		{QStringLiteral("hotkeyRelay"), QVariant::fromValue<QObject *>(&relay)},
		{QStringLiteral("clipboard"), QVariant::fromValue<QObject *>(&clipboard)},
		{QStringLiteral("pictureFiles"), QVariant::fromValue<QObject *>(&pictureFiles)},
		{QStringLiteral("help"), QVariant::fromValue<QObject *>(&helpQml)},
		{QStringLiteral("batchRun"), QVariant::fromValue<QObject *>(&batchRunQml)},
		{QStringLiteral("mediaMaker"), QVariant::fromValue<QObject *>(&mediaMakerQml)},
		{QStringLiteral("cacheManager"), QVariant::fromValue<QObject *>(&cacheManagerQml)},
		{QStringLiteral("customize"), QVariant::fromValue<QObject *>(&customizeQml)},
		{QStringLiteral("messages"), QVariant::fromValue<QObject *>(&messagesQml)},
		{QStringLiteral("display"), QVariant::fromValue<QObject *>(&displayQml)},
		{QStringLiteral("paths"), QVariant::fromValue<QObject *>(&pathsQml)},
		{QStringLiteral("firmware"), QVariant::fromValue<QObject *>(&firmwareQml)},
		{QStringLiteral("projectDetails"), QVariant::fromValue<QObject *>(&projectDetailsQml)},
		{QStringLiteral("settings"), QVariant::fromValue<QObject *>(&settingsQml)},
		{QStringLiteral("luaConsole"), QVariant::fromValue<QObject *>(&luaConsoleQml)},
		{QStringLiteral("branches"), QVariant::fromValue<QObject *>(&branchesQml)},
		{QStringLiteral("cores"), QVariant::fromValue<QObject *>(&coresQml)},
	});
	engine.load(QUrl(QStringLiteral("qrc:/chimera/Main.qml")));
	if (engine.rootObjects().isEmpty()) return 1;
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
	mainWindow = window;
	/* the place and size kept last time, when asked (Display) */
	if (display.saveWindowPosition.get())
	{
		const QStringList g = QString::fromStdString(display.windowGeometry.get()).split(QLatin1Char(','));
		if (g.size() == 4 && g[2].toInt() > 100 && g[3].toInt() > 100) window->setGeometry(g[0].toInt(), g[1].toInt(), g[2].toInt(), g[3].toInt());
	}
	QObject::connect(qApp, &QCoreApplication::aboutToQuit, [&display, window] {
		if (!display.saveWindowPosition.get() || window->visibility() == QWindow::FullScreen) return;
		const QRect r = window->geometry();
		display.windowGeometry.set(QStringLiteral("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()).toStdString());
	});
	if (customize.startFullscreen.get()) window->showFullScreen();
	KeyFilter keys(session, hotkeys);
	window->installEventFilter(&keys);
	/* --show ram-search or settings: the tool windows by name */
	const QString showName = args.value(showOpt) == QStringLiteral("ram-search") ? QStringLiteral("ramSearchWindow")
		: args.value(showOpt) == QStringLiteral("ram-watch") ? QStringLiteral("ramWatchWindow")
		: args.value(showOpt) == QStringLiteral("new-project") ? QStringLiteral("newProjectWindow")
		: args.value(showOpt) == QStringLiteral("hex") ? QStringLiteral("hexEditorWindow")
		: args.value(showOpt) == QStringLiteral("controls") ? QStringLiteral("controlsWindow")
		: args.value(showOpt) == QStringLiteral("themes") ? QStringLiteral("themesWindow")
		: args.value(showOpt) == QStringLiteral("settings") ? QStringLiteral("settingsWindow")
		: args.value(showOpt) == QStringLiteral("lua") ? QStringLiteral("luaConsoleWindow")
		: args.value(showOpt) == QStringLiteral("branches") ? QStringLiteral("branchesWindow")
		: args.value(showOpt) == QStringLiteral("undo-history") ? QStringLiteral("undoHistoryWindow")
		: args.value(showOpt) == QStringLiteral("tastudio-settings") ? QStringLiteral("tastudioSettingsWindow")
		: args.value(showOpt) == QStringLiteral("log") ? QStringLiteral("logWindow")
		: args.value(showOpt) == QStringLiteral("report") ? QStringLiteral("reportIssueWindow")
		: args.value(showOpt) == QStringLiteral("batch") ? QStringLiteral("batchRunWindow")
		: args.value(showOpt) == QStringLiteral("media") ? QStringLiteral("mediaMakerWindow")
		: args.value(showOpt) == QStringLiteral("cache") ? QStringLiteral("cacheManagerWindow")
		: args.value(showOpt) == QStringLiteral("customize") ? QStringLiteral("customizeWindow")
		: args.value(showOpt) == QStringLiteral("messages") ? QStringLiteral("messagesWindow")
		: args.value(showOpt) == QStringLiteral("sound") ? QStringLiteral("soundWindow")
		: args.value(showOpt) == QStringLiteral("display") ? QStringLiteral("displayWindow")
		: args.value(showOpt) == QStringLiteral("budgets") ? QStringLiteral("greenzoneBudgetsWindow")
		: args.value(showOpt) == QStringLiteral("paths") ? QStringLiteral("pathsWindow")
		: args.value(showOpt) == QStringLiteral("firmware") ? QStringLiteral("firmwareWindow")
		: args.value(showOpt) == QStringLiteral("project-details") ? QStringLiteral("projectDetailsWindow")
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
