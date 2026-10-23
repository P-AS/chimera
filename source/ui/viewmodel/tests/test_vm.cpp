/* The view models with no window: what a script, the MCP server or a test
 * sees. No Qt is linked here; the dispatcher is drained by hand.
 *
 *   test_vm [<synth-box package> <synth test rom> [<synth movies dir> <synth goldens dir>]]
 *
 * Without the package and rom the machine tests are skipped (exit 77 when
 * nothing else ran). */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <regex>
#include <string>
#include <thread>

#ifndef _WIN32
#include <dlfcn.h>
#endif

#include "chimera/vm/bindings.hpp"
#include "chimera/vm/config_store.hpp"
#include "chimera/vm/controls_vm.hpp"
#include "chimera/vm/hotkeys_vm.hpp"
#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/firmware.hpp"
#include "chimera/vm/hex_editor_vm.hpp"
#include "chimera/vm/branches_vm.hpp"
#include "chimera/vm/project_details_vm.hpp"
#include "chimera/vm/gamepads.hpp"
#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/lua_console_vm.hpp"
#include "chimera/vm/new_project_vm.hpp"
#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/ram_search_vm.hpp"
#include "chimera/vm/ram_watch_vm.hpp"
#include "chimera/vm/watch_values.hpp"
#include "chimera/vm/sound.hpp"
#include "chimera/vm/european_clock.hpp"
#include "chimera/vm/settings_vm.hpp"
#include "chimera/vm/themes_vm.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/engine.h"

using namespace chimera::vm;

static int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

namespace {

class ManualDispatcher : public Dispatcher
{
public:
	void post(std::function<void()> fn) override
	{
		std::lock_guard<std::mutex> lock(m_);
		q_.push_back(std::move(fn));
	}
	int pump()
	{
		std::deque<std::function<void()>> now;
		{
			std::lock_guard<std::mutex> lock(m_);
			now.swap(q_);
		}
		for (auto &fn : now) fn();
		return static_cast<int>(now.size());
	}
	/* drains until cond holds or the time runs out */
	template <class F>
	bool until(F cond, double seconds)
	{
		const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
		while (std::chrono::steady_clock::now() < end)
		{
			pump();
			if (cond()) return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		pump();
		return cond();
	}

private:
	std::mutex m_;
	std::deque<std::function<void()>> q_;
};

class Sample : public ViewModel
{
public:
	explicit Sample(Dispatcher &d) : ViewModel("sample", d)
	{
		expose("count", "a number anyone may set", count, true);
		expose("label", "a text only the view model sets", label);
		command("add", "adds to count", {"amount"}, [this](const Args &a, std::string &error) -> Value {
			int64_t n = 0;
			if (!arg(a, 0, n, error)) return {};
			count.set(count.get() + n);
			return count.get();
		}, &armed);
	}
	Property<int64_t> count{0};
	Property<std::string> label{"x"};
	Property<bool> armed{true};
};

void signalsAndProperties()
{
	Property<int64_t> p{1};
	int seen = 0;
	Connection c = p.changed.connect([&] { seen++; });
	p.set(1);
	CHECK(seen == 0);
	p.set(2);
	CHECK(seen == 1);
	c.disconnect();
	p.set(3);
	CHECK(seen == 1);

	/* a slot that disconnects another during the same emit: the other is not called */
	Signal<> s;
	int first = 0, second = 0;
	Connection c2;
	Connection c1 = s.connect([&] { first++; c2.disconnect(); });
	c2 = s.connect([&] { second++; });
	s.emit();
	CHECK(first == 1 && second == 0);

	/* a connection that outlives its signal ends quietly */
	Connection late;
	{
		Signal<int> gone;
		late = gone.connect([](int) {});
	}
	late.disconnect();
}

void registry()
{
	ManualDispatcher d;
	Sample vm(d);
	std::string error;
	CHECK(vm.get("count") && std::get<int64_t>(*vm.get("count")) == 0);
	CHECK(vm.set("count", int64_t{5}, error) && vm.count.get() == 5);
	CHECK(vm.set("count", 7.0, error) && vm.count.get() == 7); /* a script's whole real number */
	CHECK(!vm.set("count", std::string("many"), error) && error.find("cannot be") != std::string::npos);
	CHECK(!vm.set("label", std::string("y"), error) && error.find("cannot be set") != std::string::npos);
	CHECK(!vm.set("nothing", int64_t{1}, error) && error.find("no property") != std::string::npos);
	auto r = vm.run("add", {int64_t{3}}, error);
	CHECK(r && std::get<int64_t>(*r) == 10);
	CHECK(!vm.run("add", {}, error) && error.find("missing") != std::string::npos);
	vm.armed.set(false);
	CHECK(!vm.run("add", {int64_t{1}}, error) && error.find("cannot run now") != std::string::npos);
	CHECK(!vm.run("subtract", {}, error) && error.find("no command") != std::string::npos);
	CHECK(vm.properties().size() == 2 && vm.commands().size() == 1);
}

/* A script reaches a view model only through its registry */
void lua()
{
	ManualDispatcher d;
	Sample vm(d);
	LuaBridge lua;
	std::vector<std::string> lines;
	lua.onLog = [&](const std::string &l) { lines.push_back(l); };
	lua.add(vm);
	std::string error;
	CHECK(lua.run("print(chimera.sample.count, chimera.sample.label)", "t1", error));
	CHECK(lines.size() == 1 && lines[0] == "0\tx");
	CHECK(lua.run("chimera.sample.count = 4; print(chimera.sample.add(3))", "t2", error));
	CHECK(vm.count.get() == 7 && lines.back() == "7");
	/* the view model's own refusals come back as Lua errors, with its words */
	CHECK(!lua.run("chimera.sample.label = 'y'", "t3", error) && error.find("cannot be set") != std::string::npos);
	CHECK(!lua.run("chimera.sample.add('lots')", "t4", error) && error.find("argument 1 cannot be") != std::string::npos);
	CHECK(!lua.run("return chimera.sample.nothing", "t5", error) && error.find("no property or command nothing") != std::string::npos);
	CHECK(lua.run("local ok, e = pcall(chimera.sample.add); print(ok, e)", "t6", error));
	CHECK(lines.back().rfind("false\t", 0) == 0 && lines.back().find("missing") != std::string::npos);
	/* onFrame functions run on every frame; one that fails is dropped, the others stay */
	CHECK(lua.run("n = 0; chimera.onFrame(function() n = n + 1 end); chimera.onFrame(function() error('boom') end)", "t7", error));
	lua.frameDone();
	lua.frameDone();
	CHECK(lua.run("print(n)", "t8", error) && lines.back() == "2");
	int dropped = 0;
	for (const auto &l : lines) dropped += l.find("onFrame function dropped") != std::string::npos && l.find("boom") != std::string::npos;
	CHECK(dropped == 1);
	/* what an MCP server would list: the same names, from the same registry */
	const std::string d1 = lua.describe();
	CHECK(d1.find("\"name\": \"sample\"") != std::string::npos && d1.find("\"name\": \"add\"") != std::string::npos &&
		d1.find("\"params\": [\"amount\"]") != std::string::npos && d1.find("\"settable\": false") != std::string::npos);
}

/* The console: a script run there prints to its rows; a failing one says why */
void luaConsole()
{
	namespace fs = std::filesystem;
	ManualDispatcher d;
	Sample vm(d);
	LuaBridge lua;
	lua.add(vm);
	LuaConsoleViewModel console(d, lua);
	std::string error;
	RowModel &out = *console.rowLists()[0].model;
	auto lastLine = [&] {
		std::string l;
		fromValue(out.cell(out.rowCount() - 1, 0), l);
		return l;
	};
	console.set("script", std::string("print(chimera.sample.add(2) * 10)"), error);
	console.run("run", {}, error);
	CHECK(console.lines.get() == 1 && lastLine() == "20");
	console.set("script", std::string("chimera.sample.nothing()"), error);
	console.run("run", {}, error);
	CHECK(console.lines.get() == 2 && lastLine().rfind("error:", 0) == 0 && lastLine().find("no property or command nothing") != std::string::npos);
	/* saved and read back */
	const fs::path file = fs::temp_directory_path() / ("chimera-test-console-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".lua");
	console.set("script", std::string("print('from the file')"), error);
	CHECK(console.run("save", {file.string()}, error).has_value() && console.path.get() == file.string());
	console.set("script", std::string(""), error);
	CHECK(console.run("load", {file.string()}, error).has_value() && console.script.get() == "print('from the file')");
	console.run("run", {}, error);
	CHECK(lastLine() == "from the file");
	console.run("clear", {}, error);
	CHECK(console.lines.get() == 0 && out.rowCount() == 0);
	fs::remove(file);
}

/* Firmware is found by its hash when one is pinned (the project's first),
 * else by its name, else by the one file its size and name hints point at;
 * and only what the settings call for is asked for at all */
void firmware()
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-fw-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "sub");
	auto write = [&](const fs::path &p, const std::string &bytes) { std::ofstream(p, std::ios::binary) << bytes; };
	write(dir / "anything.rom", "the pinned bios");
	write(dir / "sub" / "BIOS.BIN", "named");
	write(dir / "NDS_Bios7i.bin", std::string(16, 'a'));
	write(dir / "custom.rom", "a custom bios");
	char sha[41], custom[41];
	ce_sha1_hex(reinterpret_cast<const uint8_t *>("the pinned bios"), 15, sha);
	ce_sha1_hex(reinterpret_cast<const uint8_t *>("a custom bios"), 13, custom);
	const auto index = indexFirmware({dir.string()});
	CHECK(index.size() == 4);
	auto decl = [](const std::string &text) {
		json::Json j;
		std::string e;
		json::parse(text, j, e);
		return j;
	};
	std::string how;
	const FirmwareFile *f = findFirmware(decl(std::string(R"({"id":"a","sha1":")") + sha + "\"}"), "", index, how);
	CHECK(f != nullptr && f->name == "anything.rom" && how == "by its hash");
	f = findFirmware(decl(std::string(R"({"id":"a","sha1":")") + sha + "\"}"), custom, index, how);
	CHECK(f != nullptr && f->name == "custom.rom" && how == "by the hash the project pins");
	CHECK(findFirmware(decl(R"({"id":"a","sha1":"0000000000000000000000000000000000000000","name":"BIOS.BIN"})"), "", index, how) == nullptr);
	f = findFirmware(decl(R"({"id":"b","name":"bios.bin"})"), "", index, how);
	CHECK(f != nullptr && f->name == "BIOS.BIN" && how == "by its name");
	f = findFirmware(decl(R"({"id":"c","size":16,"nameHints":["7i"]})"), "", index, how);
	CHECK(f != nullptr && f->name == "NDS_Bios7i.bin" && how == "by its size and name");
	write(dir / "other_7i.bin", std::string(16, 'b'));
	CHECK(findFirmware(decl(R"({"id":"c","size":16,"nameHints":["7i"]})"), "", indexFirmware({dir.string()}), how) == nullptr);
	/* the engine says what is needed: here only with Load BIOS on */
	const json::Json fwDecl = decl(R"([{"id":"b","display":"The BIOS","name":"bios.bin","requiredWhen":{"setting":"loadBios","is":true}}])");
	const json::Json setDecl = decl(R"([{"name":"loadBios","type":"bool","default":false}])");
	json::Json none, on, noPins;
	none.kind = json::Json::Kind::Object;
	json::parse(R"({"loadBios":true})", on, how);
	CHECK(neededFirmware(fwDecl, setDecl, "{}", none, noPins, index).empty());
	const auto needs = neededFirmware(fwDecl, setDecl, "{}", on, noPins, index);
	CHECK(needs.size() == 1 && needs[0].id == "b" && needs[0].path.find("BIOS.BIN") != std::string::npos && needs[0].sha1.size() == 40);
	fs::remove_all(dir);
}

/* What the window remembers: a property kept by its name comes back in the
 * next run; a file that does not read is set aside, not written over */
void configStore()
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const std::string file = (dir / "settings.json").string();
	ManualDispatcher d;
	{
		ConfigStore store(file);
		Sample vm(d);
		store.keep(vm, "count");
		vm.count.set(42);
		CHECK(store.problem().empty() && fs::exists(file));
	}
	{
		ConfigStore store(file);
		Sample vm(d);
		store.keep(vm, "count");
		CHECK(vm.count.get() == 42);
	}
	std::ofstream(file, std::ios::binary | std::ios::trunc) << "{ not json";
	{
		ConfigStore store(file);
		CHECK(!store.problem().empty() && fs::exists(file + ".unreadable") && !fs::exists(file));
	}
	fs::remove_all(dir);
}

void bindings()
{
	const std::string json = R"({
		"_comment": ["x"],
		"AllTrollers": {
			"Other Pad": { "P1 Up": "W" },
			"Synth Controller": { "P1 Up": "Up, J1 POV0U", "P1 A": "X", "P1 B": "Z, X", "P2 A": "Q" }
		}
	})";
	Bindings b;
	std::string error;
	CHECK(b.parse(json, {"P1 Up", "P1 A", "P1 B"}, error));
	CHECK(b.buttonsOf("Up").size() == 1 && b.buttonsOf("Up")[0] == 0);
	CHECK(b.buttonsOf("J1 POV0U").size() == 1);
	CHECK(b.buttonsOf("X").size() == 2); /* X holds A and B */
	CHECK(b.buttonsOf("W").empty());      /* the section that names fewer buttons is not taken */
	CHECK(b.buttonsOf("Q").empty());      /* a button the machine does not have */
	CHECK(!b.parse("{\"a\": [1, 2", {"P1 Up"}, error) && !error.empty());
}

void pianoRollWithoutAMachine()
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	/* the movie is the machine's: with none open there is none to fill or edit */
	auto n = roll.run("fill", {int64_t{1000000}}, error);
	CHECK(n && std::get<int64_t>(*n) == 0);
	CHECK(roll.frameCount.get() == 0);
	CHECK(roll.rowLists()[0].name == "frames" && roll.rowLists()[0].model->rowCount() == 0);
	CHECK(!roll.run("toggle", {int64_t{5}, int64_t{0}}, error) && error == "no such frame or button");
	/* the first row in view stays inside the movie */
	roll.visibleFrames.set(40);
	CHECK(roll.set("topFrame", int64_t{5000000}, error) && roll.topFrame.get() == 0);
}

/* SDL's virtual joystick stands in for a real pad: it is held, and the
 * names Chimera's bindings use come out */
bool gamepads()
{
#ifdef _WIN32
	return false;
#else
	Gamepads pads;
	if (!pads.available())
	{
		std::printf("SKIP the gamepad tests: %s\n", pads.problem().c_str());
		return false;
	}
	void *sdl = dlopen("libSDL2.so", RTLD_NOW | RTLD_NOLOAD);
	if (sdl == nullptr) sdl = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_NOLOAD);
	using Attach = int (*)(int, int, int, int);
	using SetButton = int (*)(void *, int, uint8_t);
	using SetHat = int (*)(void *, int, uint8_t);
	using SetAxis = int (*)(void *, int, int16_t);
	using Open = void *(*)(int);
	auto attach = sdl ? reinterpret_cast<Attach>(dlsym(sdl, "SDL_JoystickAttachVirtual")) : nullptr;
	auto setButton = sdl ? reinterpret_cast<SetButton>(dlsym(sdl, "SDL_JoystickSetVirtualButton")) : nullptr;
	auto setHat = sdl ? reinterpret_cast<SetHat>(dlsym(sdl, "SDL_JoystickSetVirtualHat")) : nullptr;
	auto setAxis = sdl ? reinterpret_cast<SetAxis>(dlsym(sdl, "SDL_JoystickSetVirtualAxis")) : nullptr;
	auto open = sdl ? reinterpret_cast<Open>(dlsym(sdl, "SDL_JoystickOpen")) : nullptr;
	if (!attach || !setButton || !setHat || !setAxis || !open)
	{
		std::printf("SKIP the gamepad tests: this SDL2 has no virtual joysticks\n");
		return false;
	}
	std::vector<std::string> held;
	pads.poll(held);
	const size_t before = pads.names().size();
	/* a plain joystick (type 0, not a game controller): 2 axes, 4 buttons, 1 hat */
	const int index = attach(0, 2, 4, 1);
	CHECK(index >= 0);
	pads.poll(held);
	CHECK(pads.names().size() == before + 1);
	void *joy = open(index); /* a second handle to the same device, to move it */
	CHECK(joy != nullptr);
	if (joy == nullptr) return true;
	const std::string j = "J" + std::to_string(index + 1) + " ";
	setButton(joy, 2, 1);
	setHat(joy, 0, 1 | 2); /* up and right */
	setAxis(joy, 1, -30000);
	pads.poll(held);
	auto has = [&](const std::string &n) { return std::find(held.begin(), held.end(), n) != held.end(); };
	CHECK(has(j + "B3"));
	CHECK(has(j + "POV0U") && has(j + "POV0R") && !has(j + "POV0D"));
	CHECK(has(j + "Y-") && !has(j + "Y+") && !has(j + "X+"));
	setButton(joy, 2, 0);
	setHat(joy, 0, 0);
	setAxis(joy, 1, 0);
	pads.poll(held);
	CHECK(!has(j + "B3") && !has(j + "POV0U") && !has(j + "Y-"));
	return true;
#endif
}

/* A gamepad input caught for a binding: the next one pressed, a game open
 * or not, and not one held already */
bool padCatch()
{
#ifdef _WIN32
	return false;
#else
	void *sdl = dlopen("libSDL2.so", RTLD_NOW | RTLD_NOLOAD);
	if (sdl == nullptr) sdl = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_NOLOAD);
	using Attach = int (*)(int, int, int, int);
	using SetButton = int (*)(void *, int, uint8_t);
	using Open = void *(*)(int);
	auto attach = sdl ? reinterpret_cast<Attach>(dlsym(sdl, "SDL_JoystickAttachVirtual")) : nullptr;
	auto setButton = sdl ? reinterpret_cast<SetButton>(dlsym(sdl, "SDL_JoystickSetVirtualButton")) : nullptr;
	auto open = sdl ? reinterpret_cast<Open>(dlsym(sdl, "SDL_JoystickOpen")) : nullptr;
	if (!attach || !setButton || !open)
	{
		std::printf("SKIP catching a gamepad input: no SDL2 with virtual joysticks\n");
		return false;
	}
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	std::string error;
	const int index = attach(0, 0, 4, 0);
	void *joy = index >= 0 ? open(index) : nullptr;
	CHECK(joy != nullptr);
	if (joy == nullptr) return true;
	const std::string j = "J" + std::to_string(index + 1) + " ";
	/* a button held when the catch starts is not the one pressed */
	setButton(joy, 0, 1);
	CHECK(session.run("catchPad", {}, error).has_value() && session.catchingPad.get() && session.caughtPad.get().empty());
	CHECK(!d.until([&] { return !session.caughtPad.get().empty(); }, 1));
	setButton(joy, 3, 1);
	CHECK(d.until([&] { return !session.caughtPad.get().empty(); }, 10));
	CHECK(session.caughtPad.get() == j + "B4" && !session.catchingPad.get());
	setButton(joy, 3, 0);
	/* stopped, nothing is caught */
	CHECK(session.run("catchPad", {}, error).has_value() && session.run("stopCatchPad", {}, error).has_value());
	setButton(joy, 2, 1);
	CHECK(!d.until([&] { return !session.caughtPad.get().empty(); }, 1) && !session.catchingPad.get());
	setButton(joy, 0, 0);
	setButton(joy, 2, 0);
	return true;
#endif
}

/* How fast the machine runs, and its sound, as Chimera has them: its
 * speeds a step at a time, the volume in tens; then the machine itself at a
 * quarter of its speed, at four times it, and as fast as it can */
bool speedAndSound(const char *package, const char *rom)
{
	std::vector<int16_t> out;
	const int16_t samples[] = {1000, -1000, 32767, -32768};
	Sound::scale(samples, 2, 50, out);
	CHECK(out == std::vector<int16_t>({500, -500, 16383, -16384}));
	Sound::scale(samples, 2, 0, out);
	CHECK(out == std::vector<int16_t>({0, 0, 0, 0}));
	Sound::scale(samples, 2, 150, out);
	CHECK(out == std::vector<int16_t>({1000, -1000, 32767, -32768}));

	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	std::string error;
	auto step = [&](const char *command) {
		session.run(command, {}, error);
		return session.speedPercent.get();
	};
	CHECK(step("speedUp") == 150 && step("speedUp") == 200 && step("speedDown") == 150 && step("speedReset") == 100);
	CHECK(session.status.get() == "Speed: 100%");
	session.speedPercent.set(1);
	CHECK(step("speedDown") == 1 && step("speedUp") == 3);
	session.speedPercent.set(6400);
	CHECK(step("speedUp") == 6400 && step("speedDown") == 3200);
	session.volume.set(95);
	session.run("volumeUp", {}, error);
	CHECK(session.volume.get() == 100 && session.status.get() == "Volume 100");
	for (int i = 0; i < 12; i++) session.run("volumeDown", {}, error);
	CHECK(session.volume.get() == 0);
	session.run("toggleSound", {}, error);
	CHECK(!session.soundOn.get() && session.status.get() == "Sound off");
	session.run("toggleThrottle", {}, error);
	CHECK(session.unthrottled.get() && session.status.get() == "Unthrottled");
	session.run("toggleThrottle", {}, error);

	session.run("open", {std::string(package), std::string(rom)}, error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	/* frames run in a stretch of time, at a speed (the machine runs at 60 a second) */
	auto framesIn = [&](double seconds) {
		const int64_t from = session.frame.get();
		session.run("play", {}, error);
		d.until([] { return false; }, seconds);
		session.run("pause", {}, error);
		d.until([&] { return !session.running.get(); }, 5);
		return session.frame.get() - from;
	};
	session.speedPercent.set(25);
	const int64_t quarter = framesIn(1.0);
	session.speedPercent.set(400);
	const int64_t four = framesIn(1.0);
	session.speedPercent.set(100);
	session.unthrottled.set(true);
	const int64_t free = framesIn(0.5);
	session.unthrottled.set(false);
	std::printf("speed: %lld frames a second at 25%%, %lld at 400%%, %lld in half a second unthrottled\n",
		static_cast<long long>(quarter), static_cast<long long>(four), static_cast<long long>(free));
	CHECK(quarter >= 8 && quarter <= 25);
	CHECK(four >= 120 && four <= 260);
	CHECK(free > 260);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* Chimera's counters over the picture: the frame counter, the lag counter,
 * the input of the frame just run and the speed, worked out as Chimera
 * writes them */
bool overlay(const char *package, const char *rom)
{
	{
		/* players in turn, the console's last; buttons, then analog controls */
		const std::vector<std::string> buttons = {"Power", "P1 Up", "P2 A", "P1 B", "P10 C"};
		std::vector<SessionViewModel::Axis> axes(3);
		axes[0].name = "P2 Dial";
		axes[1].name = "Slider";
		axes[2].name = "P1 Paddle";
		axes[2].neutral = 128;
		const uint64_t mask = 1 | 4 | 8;
		CHECK(SessionViewModel::inputDisplay(buttons, "PUABC", axes, mask, {5, 0, 128}) == " B      A    5, P      ");
		CHECK(SessionViewModel::inputDisplay(buttons, "PUABC", axes, 0, {-1234, 7, 0}) == "      0, -1234,      7,");
	}
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	std::string error;
	session.run("open", {std::string(package), std::string(rom)}, error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	session.showFrameCounter.set(true);
	session.showInput.set(true);
	session.showLagCounter.set(true);
	session.showFps.set(true);
	CHECK(session.frameCounterText.get() == "0/0" && session.inputText.get().empty() && session.lagCount.get() == 0);
	/* three frames with Down held, then one with nothing */
	session.key("Down", true);
	for (int i = 0; i < 3; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 3; }, 20));
	session.key("Down", false);
	std::printf("overlay: [%s] [%s] lags %lld mnemonics [%s]\n", session.frameCounterText.get().c_str(), session.inputText.get().c_str(),
		static_cast<long long>(session.lagCount.get()), session.mnemonics().c_str());
	CHECK(session.frameCounterText.get() == "3/2 (Finished)");
	CHECK(session.inputText.get() == std::string(" ") + session.mnemonics()[1] + "      ");
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 4; }, 20));
	CHECK(session.inputText.get() == "        " && session.frameCounterText.get() == "4/2 (Finished)");
	/* the lag frames are the machine's, counted up to the frame */
	int64_t lags = 0;
	for (int64_t f = 0; f < 4; f++) lags += inputs.lagAt(f) ? 1 : 0;
	CHECK(session.lagCount.get() == lags && session.lagFrame.get() == inputs.lagAt(3));
	/* lag frames the counter did not hear of while it was off are counted
	 * when it is on again (this game reads its input every frame: the
	 * flags are set here), and a frame run adds its own */
	session.showLagCounter.set(false);
	inputs.setLag(1, true);
	inputs.setLag(3, true);
	session.showLagCounter.set(true);
	CHECK(lags == 0 && session.lagCount.get() == 2);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 5; }, 20));
	CHECK(session.lagCount.get() == 2 + (inputs.lagAt(0) ? 1 : 0) + (inputs.lagAt(2) ? 1 : 0) + (inputs.lagAt(4) ? 1 : 0));
	CHECK(session.frameCounterText.get() == "5/2 (Finished)");
	CHECK(session.fpsText.get().size() > 4 && session.fpsText.get().substr(session.fpsText.get().size() - 4) == " fps");
	/* a frame with input past the last makes it the last; back before it, not finished */
	session.run("seek", {int64_t{1}}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 20));
	CHECK(session.frameCounterText.get() == "1/2" && session.inputText.get() == std::string(" ") + session.mnemonics()[1] + "      ");
	CHECK(session.lagCount.get() == (inputs.lagAt(0) ? 1 : 0));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

std::string readFile(const std::filesystem::path &p);

/* The piano roll's columns shown and hidden, as Chimera's TAStudio keeps
 * them: its default-hidden controls, and the project's own list in
 * Chimera's shape, written where the project had none and only marked
 * shown or hidden where it had its own */
bool pianoRollColumns(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	using json::Json;
	CHECK(SessionViewModel::hiddenByDefault("Power") && SessionViewModel::hiddenByDefault("Reset") &&
		SessionViewModel::hiddenByDefault("Key A") && SessionViewModel::hiddenByDefault("Eject Disk") &&
		SessionViewModel::hiddenByDefault("Play Tape") && SessionViewModel::hiddenByDefault("Tilt X") &&
		!SessionViewModel::hiddenByDefault("P1 Up") && !SessionViewModel::hiddenByDefault("P1 Keyboard"));
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-columns-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	const fs::path fresh = dir / "fresh.chimeraProject", own = dir / "own.chimeraProject";
	auto tastudioOf = [](const fs::path &path) {
		const char *err = nullptr;
		ce_project *p = ce_project_open(path.string().c_str(), &err);
		std::string t;
		if (p != nullptr)
		{
			uint64_t len = 0;
			const char *text = ce_project_tastudio_text(p, &len);
			if (text != nullptr) t.assign(text, static_cast<size_t>(len));
			ce_project_free(p);
		}
		return t;
	};
	{
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		PianoRollViewModel roll(d, inputs, session);
		std::string error;
		session.run("open", {std::string(package), (dir / "walker.testrom").string()}, error);
		CHECK(d.until([&] { return session.isOpen.get() && roll.frames().columnCount() == 8; }, 60));
		CHECK(roll.hiddenColumns.get().empty());
		CHECK(roll.run("hideColumn", {std::string("P1 B")}, error).has_value() && roll.hiddenColumns.get() == "P1 B");
		CHECK(roll.frames().columnHidden(5) && !roll.frames().columnHidden(4));
		RowModel *columns = nullptr;
		for (const auto &r : roll.rowLists())
			if (r.name == "columns") columns = r.model;
		bool shown = true;
		CHECK(columns != nullptr && columns->rowCount() == 8);
		if (columns != nullptr) fromValue(columns->cell(5, 2), shown);
		CHECK(!shown);
		CHECK(session.run("saveAsProject", {fresh.string()}, error).has_value());
		CHECK(d.until([&] { return session.projectPath.get() == fresh.string(); }, 20));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	/* written in Chimera's shape: one roll, the cursor and frame columns, then the controls */
	{
		Json t;
		std::string jerr;
		CHECK(json::parse(tastudioOf(fresh), t, jerr));
		const Json *o = t.find("o");
		const Json *rolls = o != nullptr ? o->find("Columns") : nullptr;
		CHECK(rolls != nullptr && rolls->items.size() == 1 && rolls->items[0].items.size() == 10);
		if (rolls != nullptr && rolls->items.size() == 1 && rolls->items[0].items.size() == 10)
		{
			const Json &cs = rolls->items[0];
			CHECK(cs.items[0].find("Name")->text == "CursorColumn" && cs.items[1].find("Text")->text == "Frame#");
			CHECK(cs.items[2].find("Name")->text == "P1 Up" && cs.items[2].find("Text")->text == "U" && cs.items[2].find("Visible")->boolean);
			CHECK(cs.items[7].find("Name")->text == "P1 B" && !cs.items[7].find("Visible")->boolean);
		}
	}
	/* a project with Chimera's own columns (widths and all): its hidden ones
	 * read, and only shown or hidden changed when saved */
	{
		const char *err = nullptr;
		ce_project *p = ce_project_open(fresh.string().c_str(), &err);
		CHECK(p != nullptr);
		if (p == nullptr) return true;
		const std::string chimeraOwn = "{\"o\": {\"$type\": \"Chimera.Client.GUI.TAStudio+MovieClientSettings, Chimera.Client.GUI\", "
			"\"HorizontalOrientation\": false, \"Columns\": [[{\"VerticalWidth\": 12, \"HorizontalHeight\": 12, \"Left\": 0, \"Right\": 14, "
			"\"Name\": \"CursorColumn\", \"Text\": \"\", \"Visible\": true, \"Emphasis\": false, \"Rotatable\": false}, "
			"{\"VerticalWidth\": 15, \"HorizontalHeight\": 10, \"Left\": 69, \"Right\": 87, \"Name\": \"P1 Up\", \"Text\": \"U\", "
			"\"Visible\": false, \"Emphasis\": true, \"Rotatable\": false}, "
			"{\"VerticalWidth\": 15, \"HorizontalHeight\": 10, \"Left\": 87, \"Right\": 105, \"Name\": \"P1 Down\", \"Text\": \"D\", "
			"\"Visible\": true, \"Emphasis\": false, \"Rotatable\": false}]], \"GreenzonePeriod\": 1}}";
		CHECK(ce_project_set_tastudio_text(p, chimeraOwn.c_str(), &err) == 0);
		CHECK(ce_project_save(p, own.string().c_str(), &err) == 0);
		ce_project_free(p);
	}
	{
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		PianoRollViewModel roll(d, inputs, session);
		std::string error;
		session.run("openProject", {own.string(), std::string(package)}, error);
		CHECK(d.until([&] { return session.isOpen.get() && roll.hiddenColumns.get() == "P1 Up"; }, 60));
		CHECK(roll.run("showColumn", {std::string("P1 Up")}, error).has_value());
		CHECK(roll.run("hideColumn", {std::string("P1 Down")}, error).has_value());
		CHECK(session.run("saveProject", {}, error).has_value());
		CHECK(d.until([&] { return session.status.get().rfind("Saved", 0) == 0; }, 20));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	{
		Json t;
		std::string jerr;
		CHECK(json::parse(tastudioOf(own), t, jerr));
		const Json *o = t.find("o");
		const Json *rolls = o != nullptr ? o->find("Columns") : nullptr;
		CHECK(rolls != nullptr && rolls->items.size() == 1 && rolls->items[0].items.size() == 3);
		if (rolls != nullptr && rolls->items.size() == 1 && rolls->items[0].items.size() == 3)
		{
			const Json &up = rolls->items[0].items[1], &down = rolls->items[0].items[2];
			CHECK(up.find("Visible")->boolean && up.find("Emphasis")->boolean && up.find("VerticalWidth")->number() == 15);
			CHECK(!down.find("Visible")->boolean && down.find("Right")->number() == 105);
		}
	}
	fs::remove_all(dir);
	return true;
}

/* Chimera's autohold and autofire: armed by their hotkey, a button pressed
 * changes over; held or fired, it goes into the frames run */
bool autoholdAndAutofire(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	std::string error;
	session.run("open", {std::string(package), std::string(rom)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && session.buttonNames().size() == 8; }, 60));
	/* the key the package gives P1 A, and Down's */
	std::string keyA = session.defaultBindingTexts()[4];
	keyA = keyA.substr(0, keyA.find(','));
	auto press = [&](const std::string &k) {
		session.key(k, true);
		session.key(k, false);
	};
	auto advance = [&](int n) {
		const int64_t to = session.frame.get() + n;
		for (int i = 0; i < n; i++) session.run("frameAdvance", {}, error);
		return d.until([&] { return session.frame.get() == to; }, 20);
	};
	/* not armed, a press is only a press */
	press(keyA);
	CHECK(session.autoheld.get().empty());
	session.armAutohold(true);
	CHECK(session.stickyArmed.get());
	press(keyA);
	session.armAutohold(false);
	CHECK(session.autoheld.get() == "P1 A" && !session.stickyArmed.get() && session.stickyText.get() == "Held: P1 A ");
	CHECK(advance(2));
	CHECK((inputs.at(0) >> 4 & 1) && (inputs.at(1) >> 4 & 1));
	/* autofire: on a frame, off a frame */
	session.armAutofire(true);
	press("Down");
	session.armAutofire(false);
	CHECK(session.autofired.get() == "P1 Down" && session.stickyText.get() == "Held: P1 A Auto-P1 Down ");
	CHECK(advance(4));
	CHECK((inputs.at(2) >> 1 & 1) && !(inputs.at(3) >> 1 & 1) && (inputs.at(4) >> 1 & 1) && !(inputs.at(5) >> 1 & 1));
	CHECK((inputs.at(5) >> 4 & 1));
	/* on two, off one */
	session.autofireOn.set(2);
	session.run("toggleAutofire", {std::string("P1 Down")}, error);
	session.run("toggleAutofire", {std::string("P1 Down")}, error);
	CHECK(advance(3));
	CHECK((inputs.at(6) >> 1 & 1) && (inputs.at(7) >> 1 & 1) && !(inputs.at(8) >> 1 & 1));
	/* armed again, the held one lets go */
	session.armAutohold(true);
	press(keyA);
	session.armAutohold(false);
	CHECK(session.autoheld.get().empty());
	CHECK(session.run("clearAutohold", {}, error).has_value() && session.autofired.get().empty() && session.status.get() == "Autohold keys cleared");
	CHECK(advance(1));
	CHECK(inputs.at(9) == 0);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* A project's backup, the greenzone cleared, and the state history check,
 * as Chimera's TAStudio has them */
bool backupAndHistory(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-backup-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	std::string error;
	session.backupFolder.set((dir / "backup").string());
	session.run("open", {std::string(package), (dir / "walker.testrom").string()}, error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	CHECK(!session.run("saveBackup", {}, error));
	const fs::path project = dir / "walker.chimeraProject";
	CHECK(session.run("saveAsProject", {project.string()}, error).has_value());
	CHECK(d.until([&] { return session.projectPath.get() == project.string(); }, 20));
	session.key("Down", true);
	for (int i = 0; i < 30; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 30; }, 20));
	session.key("Down", false);
	/* the backup: named for the project, the date and the time; the project's own file left as it is */
	const std::string before = readFile(project);
	const auto made = session.run("saveBackup", {}, error);
	std::string backup;
	CHECK(made.has_value() && fromValue(*made, backup));
	CHECK(d.until([&] { return session.status.get() == "Backup saved: " + backup; }, 20));
	CHECK(std::regex_match(fs::path(backup).filename().string(), std::regex("walker\\.\\d{4}-\\d\\d-\\d\\d \\d\\d\\.\\d\\d\\.\\d\\d\\.chimeraProject")));
	CHECK(fs::exists(backup) && fs::path(backup).parent_path() == dir / "backup");
	CHECK(readFile(project) == before && session.projectPath.get() == project.string());
	/* the backup is the project as it stood: its 30 frames */
	{
		const char *err = nullptr;
		ce_project *p = ce_project_open(backup.c_str(), &err);
		uint64_t len = 0;
		const char *log = p != nullptr ? ce_project_log_text(p, &len) : nullptr;
		int64_t lines = 0;
		for (uint64_t i = 0; log != nullptr && i < len; i++) lines += log[i] == '|' && (i == 0 || log[i - 1] == '\n') ? 1 : 0;
		CHECK(lines == 30);
		if (p != nullptr) ce_project_free(p);
	}
	/* the state history check: every state puts the machine back exactly */
	CHECK(session.run("checkStateHistory", {(dir / "integrity").string()}, error).has_value());
	CHECK(d.until([&] { return session.status.get().rfind("The state history check passed", 0) == 0; }, 60));
	std::printf("history: %s\n", session.status.get().c_str());
	CHECK(!fs::exists(dir / "integrity"));
	/* the greenzone cleared: the first frame's state kept, the machine at frame 0 */
	CHECK(session.greenzoneStates.get() > 1);
	CHECK(session.run("clearGreenzone", {}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 0 && session.greenzoneStates.get() <= 1; }, 20));
	CHECK(session.run("checkStateHistory", {}, error).has_value());
	CHECK(d.until([&] { return session.status.get() == "The state history holds nothing to check yet"; }, 20));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir);
	return true;
}

/* TAStudio's settings for playing, saving, copying, markers and branches,
 * and the project's changes not saved yet: marked by every kind of edit,
 * cleared by a save, saved by the autosave when it comes round */
bool tastudioSettings(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-tastudio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	BranchesViewModel branches(d, inputs, session, roll);
	ProjectDetailsViewModel details(d, session);
	std::string error;
	session.backupFolder.set((dir / "backup").string());
	auto backups = [&] {
		int64_t n = 0;
		std::error_code ec;
		for (fs::directory_iterator it(dir / "backup", ec), end; !ec && it != end; it.increment(ec)) n++;
		return n;
	};
	auto saved = [&] {
		session.run("saveProject", {}, error);
		return d.until([&] { return !session.unsaved.get(); }, 20);
	};
	session.run("open", {std::string(package), (dir / "walker.testrom").string()}, error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	/* a game alone has nothing to save */
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 20));
	d.until([] { return false; }, 0.1);
	CHECK(!session.unsaved.get());
	const fs::path project = dir / "walker.chimeraProject";
	CHECK(session.run("saveAsProject", {project.string()}, error).has_value());
	CHECK(d.until([&] { return session.projectPath.get() == project.string(); }, 20));
	CHECK(!session.unsaved.get());
	/* frames run past the movie's end are a change */
	session.key("Down", true);
	for (int i = 0; i < 19; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 20; }, 20));
	session.key("Down", false);
	CHECK(d.until([&] { return session.unsaved.get(); }, 5));
	CHECK(saved());
	/* an edit; and a save by hand writes a backup as well when asked */
	roll.run("paint", {int64_t{2}, int64_t{3}, int64_t{4}, true}, error);
	CHECK(session.unsaved.get());
	session.backupOnSave.set(true);
	CHECK(saved());
	CHECK(d.until([&] { return backups() == 1; }, 20));
	session.backupOnSave.set(false);
	/* a marker keeps its frame's state, or not; one named again keeps what it kept */
	RowModel *markerRows = nullptr;
	for (const auto &r : roll.rowLists())
		if (r.name == "markers") markerRows = r.model;
	CHECK(markerRows != nullptr);
	if (markerRows == nullptr) return false;
	auto keeps = [&](int64_t frame) {
		for (int64_t i = 0; i < markerRows->rowCount(); i++)
		{
			int64_t f = -1;
			bool k = false;
			if (fromValue(markerRows->cell(i, 0), f) && f == frame && fromValue(markerRows->cell(i, 2), k)) return k ? 1 : 0;
		}
		return -1;
	};
	roll.markersKeepStates.set(false);
	CHECK(roll.run("addMarker", {int64_t{5}, std::string("five")}, error).has_value() && keeps(5) == 0);
	CHECK(session.unsaved.get());
	roll.markersKeepStates.set(true);
	CHECK(roll.run("addMarker", {int64_t{5}, std::string("named again")}, error).has_value() && keeps(5) == 0);
	CHECK(roll.run("addMarker", {int64_t{6}, std::string("six")}, error).has_value() && keeps(6) == 1);
	/* Copy: a line a frame, as the movie has them; with their numbers when asked */
	CHECK(roll.run("copyFrames", {int64_t{2}, int64_t{3}}, error).has_value());
	const std::string plain = roll.clipboardText.get();
	const std::vector<std::string> lines = inputs.lines(2, 3);
	CHECK(lines.size() == 2 && plain == lines[0] + "\n" + lines[1] + "\n" && plain[0] == '|');
	roll.copyFrameNumbers.set(true);
	roll.run("copyFrames", {int64_t{2}, int64_t{3}}, error);
	const std::string numbered = roll.clipboardText.get();
	CHECK(numbered == "00002 " + lines[0] + "\n00003 " + lines[1] + "\n");
	roll.copyFrameNumbers.set(false);
	/* pasted from text: a line that is not a frame's input pastes nothing */
	const uint64_t at10 = roll.frameBits(10), at11 = roll.frameBits(11), at2 = roll.frameBits(2), at3 = roll.frameBits(3);
	CHECK(at2 != at10);
	CHECK(!roll.run("pasteText", {int64_t{10}, numbered, false}, error).has_value());
	CHECK(roll.frameBits(10) == at10 && roll.frameBits(11) == at11);
	error.clear();
	CHECK(roll.run("pasteText", {int64_t{10}, plain, false}, error).has_value() && error.empty());
	CHECK(roll.frameBits(10) == at2 && roll.frameBits(11) == at3 && inputs.size() == 20);
	/* Windows line ends, put in */
	CHECK(roll.run("pasteText", {int64_t{0}, lines[0] + "\r\n" + lines[1] + "\r\n", true}, error).has_value());
	CHECK(inputs.size() == 22 && roll.frameBits(0) == at2 && roll.frameBits(1) == at3);
	/* following the current frame, the four ways TAStudio scrolls to it */
	{
		auto seekTo = [&](int64_t f) {
			session.run("seek", {f}, error);
			return d.until([&] { return session.frame.get() == f && roll.currentFrame.get() == f; }, 20);
		};
		roll.visibleFrames.set(5);
		roll.topFrame.set(0);
		CHECK(seekTo(12) && roll.topFrame.get() == 8);
		CHECK(seekTo(3) && roll.topFrame.get() == 3);
		CHECK(seekTo(5) && roll.topFrame.get() == 3);
		roll.followScroll.set("top");
		CHECK(seekTo(20) && roll.topFrame.get() == 20);
		roll.followScroll.set("center");
		CHECK(seekTo(10) && roll.topFrame.get() == 8);
		CHECK(seekTo(11) && roll.topFrame.get() == 8);
		roll.followAlways.set(true);
		CHECK(seekTo(12) && roll.topFrame.get() == 10);
		roll.followAlways.set(false);
		roll.followScroll.set("bottom");
		CHECK(seekTo(20) && roll.topFrame.get() == 16);
		roll.followScroll.set("near");
		roll.visibleFrames.set(0);
	}
	/* pause at the movie's end: once, and played on from there it goes on */
	session.pauseAtEnd.set(true);
	session.run("seek", {int64_t{15}}, error);
	CHECK(d.until([&] { return session.frame.get() == 15; }, 20));
	session.run("play", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 22 && !session.running.get(); }, 20));
	d.until([] { return false; }, 0.3);
	CHECK(session.frame.get() == 22 && !session.running.get());
	session.run("play", {}, error);
	CHECK(d.until([&] { return session.frame.get() > 26; }, 20));
	session.run("pause", {}, error);
	CHECK(d.until([&] { return !session.running.get(); }, 5));
	session.pauseAtEnd.set(false);
	/* rewind goes back its step, and its fast step while fast forwarding */
	session.run("seek", {int64_t{20}}, error);
	CHECK(d.until([&] { return session.frame.get() == 20; }, 20));
	session.rewindStep.set(3);
	session.holdRewind(true);
	CHECK(d.until([&] { return session.frame.get() == 17; }, 20));
	session.holdRewind(false);
	session.rewindStepFast.set(5);
	session.holdFastForward(true);
	session.holdRewind(true);
	CHECK(d.until([&] { return session.frame.get() == 12; }, 20));
	session.holdRewind(false);
	session.holdFastForward(false);
	d.until([] { return false; }, 0.2);
	CHECK(session.frame.get() == 12);
	session.rewindStep.set(1);
	session.rewindStepFast.set(4);
	/* a branch made is a change; the old way of loading one only goes to its frame */
	CHECK(saved());
	CHECK(branches.run("create", {}, error).has_value());
	CHECK(d.until([&] { return branches.made.get() == 1; }, 20));
	CHECK(session.unsaved.get());
	CHECK(saved());
	roll.run("toggle", {int64_t{1}, int64_t{4}}, error);
	const std::string edited = inputs.text();
	session.run("seek", {int64_t{3}}, error);
	CHECK(d.until([&] { return session.frame.get() == 3; }, 20));
	branches.oldControlScheme.set(true);
	CHECK(branches.run("load", {int64_t{0}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 12; }, 20));
	CHECK(inputs.text() == edited);
	branches.oldControlScheme.set(false);
	/* the project's details are a change */
	CHECK(saved());
	CHECK(details.run("setComments", {std::string("autosaved")}, error).has_value() && session.unsaved.get());
	/* the autosave: the project saved by itself */
	session.autosaveSeconds.set(1);
	CHECK(d.until([&] { return !session.unsaved.get(); }, 20));
	{
		const char *err = nullptr;
		ce_project *p = ce_project_open(project.string().c_str(), &err);
		CHECK(p != nullptr && std::string(ce_project_description(p) != nullptr ? ce_project_description(p) : "") == "autosaved");
		if (p != nullptr) ce_project_free(p);
	}
	/* as a backup: one written, and the project still unsaved */
	session.autosaveAsBackup.set(true);
	const int64_t before = backups();
	roll.run("toggle", {int64_t{2}, int64_t{4}}, error);
	CHECK(d.until([&] { return backups() > before; }, 20));
	CHECK(session.unsaved.get());
	session.autosaveSeconds.set(0);
	session.autosaveAsBackup.set(false);
	/* a column's Auto-hold while the game plays: held, fired, or its own
	 * pattern, as "Choose pattern" says; kept in the project as Chimera's
	 * column emphasis */
	{
		const std::string a = session.buttonNames()[4];
		session.autofireSkipsLag.set(false);
		auto recorded = [&](int n) {
			const int64_t end = inputs.size();
			session.run("seek", {end}, error);
			d.until([&] { return session.frame.get() == end; }, 20);
			for (int i = 0; i < n; i++) session.run("frameAdvance", {}, error);
			d.until([&] { return session.frame.get() == end + n; }, 20);
			std::string bits;
			for (int64_t f = end; f < end + n; f++) bits += (inputs.at(f) >> 4 & 1) ? '1' : '0';
			return bits;
		};
		CHECK(roll.run("toggleAutoHold", {a}, error) == Value(true));
		CHECK(recorded(4) == "1111");
		roll.patternKind.set("autofire");
		CHECK(recorded(4) == "1010");
		roll.patternKind.set("custom");
		CHECK(roll.run("setPattern", {a, std::string("On x2, Off x1"), int64_t{0}, false}, error).has_value());
		CHECK(recorded(6) == "110110");
		roll.patternKind.set("hold");
		CHECK(roll.run("toggleAutoHold", {a}, error) == Value(false));
		CHECK(recorded(3) == "000");
		CHECK(roll.run("toggleAutoHold", {a}, error) == Value(true));
		session.autofireSkipsLag.set(true);
	}
	/* a control's pattern is kept in the project, as Chimera keeps it */
	CHECK(roll.run("setPattern", {session.buttonNames()[4], std::string("On x3, Off x2"), int64_t{0}, false}, error).has_value());
	CHECK(session.unsaved.get() && saved());
	{
		const char *err = nullptr;
		ce_project *p = ce_project_open(project.string().c_str(), &err);
		uint64_t len = 0;
		const char *t = p != nullptr ? ce_project_tastudio_text(p, &len) : nullptr;
		const std::string tastudio = t != nullptr ? std::string(t, static_cast<size_t>(len)) : "";
		CHECK(tastudio.find("\"BoolPatterns\":[") != std::string::npos &&
			tastudio.find("{\"_index\":0,\"SkipsLag\":false,\"Pattern\":[true,true,true,false,false],\"Loop\":0}") != std::string::npos);
		if (p != nullptr) ce_project_free(p);
	}
	/* closed, nothing is unsaved */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	CHECK(!session.unsaved.get());
	/* opened again, the pattern is the project's */
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get(); }, 60));
	{
		RowModel *patterns = nullptr;
		for (const auto &r : roll.rowLists())
			if (r.name == "patterns") patterns = r.model;
		std::string runs;
		bool skips = true;
		CHECK(patterns != nullptr && d.until([&] { return fromValue(patterns->cell(4, 1), runs) && runs == "On x3, Off x2"; }, 20));
		CHECK(patterns != nullptr && fromValue(patterns->cell(4, 3), skips) && !skips);
		CHECK(patterns != nullptr && fromValue(patterns->cell(3, 1), runs) && runs == "On x1, Off x1");
		/* and the Auto-hold column is one again */
		RowModel *columns = nullptr;
		for (const auto &r : roll.rowLists())
			if (r.name == "columns") columns = r.model;
		bool held = false;
		CHECK(columns != nullptr && fromValue(columns->cell(4, 3), held) && held);
		CHECK(columns != nullptr && fromValue(columns->cell(3, 3), held) && !held);
		/* the machine started again: still the project's */
		const int64_t opened = session.opens.get();
		session.run("restart", {}, error);
		CHECK(d.until([&] { return session.opens.get() > opened && session.isOpen.get(); }, 60));
		d.until([] { return false; }, 0.2);
		CHECK(patterns != nullptr && fromValue(patterns->cell(4, 1), runs) && runs == "On x3, Off x2");
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir);
	return true;
}

/* The project's details as Chimera's TAStudio edits them - its header,
 * comments and subtitles - kept with the project, and the subtitles of the
 * frame on screen worked out */
bool projectDetails(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-details-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	const fs::path project = dir / "walker.chimeraProject";
	{
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		ProjectDetailsViewModel details(d, session);
		std::string error;
		CHECK(!details.run("setComments", {std::string("none")}, error) && error == "no project is open");
		session.run("open", {std::string(package), (dir / "walker.testrom").string()}, error);
		CHECK(d.until([&] { return session.isOpen.get(); }, 60));
		CHECK(session.run("saveAsProject", {project.string(), std::string("the corridor")}, error).has_value());
		CHECK(d.until([&] { return session.projectPath.get() == project.string(); }, 20));
		CHECK(details.run("reload", {}, error).has_value() && details.gameName.get() == "the corridor" && details.core.get() == "Synth");
		CHECK(details.run("setHeader", {std::string("a TASer"), std::string("1.0"), std::string("SYNTH-01"), std::string("Walker"), false}, error).has_value());
		CHECK(details.author.get() == "a TASer" && details.gameName.get() == "Walker" && details.defaultAuthor.get() == "default user");
		CHECK(details.run("setComments", {std::string("first\n\n  \nsecond\r\n")}, error).has_value() && details.comments.get() == "first\nsecond");
		CHECK(details.run("addSubtitle", {int64_t{30}, int64_t{4}, int64_t{8}, int64_t{60}, std::string("#FF0000"), std::string("later")}, error).has_value());
		CHECK(details.run("addSubtitle", {int64_t{0}, int64_t{1}, int64_t{2}, int64_t{10}, std::string("#80FFFFFF"), std::string("at once")}, error).has_value());
		CHECK(!details.run("addSubtitle", {int64_t{0}, int64_t{1}, int64_t{2}, int64_t{0}, std::string("#FFFFFF"), std::string("never")}, error));
		CHECK(!details.run("addSubtitle", {int64_t{0}, int64_t{1}, int64_t{2}, int64_t{5}, std::string("red"), std::string("x")}, error) &&
			error == "red is not a colour: #RRGGBB or #AARRGGBB");
		/* kept by frame */
		RowModel &subs = *details.rowLists()[0].model;
		auto cellText = [&](int64_t r, int32_t c) {
			std::string t;
			const Value v = subs.cell(r, c);
			if (!fromValue(v, t))
			{
				int64_t n = 0;
				fromValue(v, n);
				t = std::to_string(n);
			}
			return t;
		};
		CHECK(details.count.get() == 2 && cellText(0, 5) == "at once" && cellText(1, 5) == "later" && cellText(0, 4) == "#80FFFFFF" && cellText(1, 4) == "#FFFF0000");
		/* on screen at frame 0: the first, in its colour (shown opaque) */
		CHECK(details.onScreen.get() == "[{\"text\": \"at once\", \"x\": 1, \"y\": 2, \"color\": \"#FFFFFFFF\"}]");
		CHECK(details.run("editSubtitle", {int64_t{1}, int64_t{0}, int64_t{4}, int64_t{8}, int64_t{60}, std::string("#00FF00"), std::string("now too")}, error).has_value());
		CHECK(details.onScreen.get().find("now too") != std::string::npos);
		details.showSubtitles.set(false);
		CHECK(details.onScreen.get() == "[]");
		details.showSubtitles.set(true);
		CHECK(details.run("removeSubtitle", {int64_t{0}}, error).has_value() && details.count.get() == 1);
		CHECK(session.run("saveProject", {}, error).has_value());
		CHECK(d.until([&] { return session.status.get().rfind("Saved", 0) == 0; }, 20));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	const std::string text = readFile(project);
	CHECK(text.find("a TASer") != std::string::npos && text.find("SYNTH-01") != std::string::npos && text.find("second") != std::string::npos);
	{
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		ProjectDetailsViewModel details(d, session);
		std::string error;
		session.run("openProject", {project.string(), std::string(package)}, error);
		CHECK(d.until([&] { return session.isOpen.get() && details.core.get() == "Synth"; }, 60));
		CHECK(details.author.get() == "a TASer" && details.emulatorVersion.get() == "1.0" && details.boardName.get() == "SYNTH-01");
		CHECK(details.gameName.get() == "Walker" && details.comments.get() == "first\nsecond" && details.count.get() == 1);
		CHECK(details.subtitles()[0].message == "now too" && details.subtitles()[0].color == 0xFF00FF00u && details.subtitles()[0].frame == 0);
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	fs::remove_all(dir);
	return true;
}

std::string readFile(const std::filesystem::path &p);

bool machine(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;

	session.run("open", {std::string(package), std::string(rom)}, error);
	CHECK(error.empty());
	if (!d.until([&] { return session.isOpen.get() || session.status.get().rfind("Could not", 0) == 0; }, 60))
	{
		std::fprintf(stderr, "FAIL: the machine did not open: %s\n", session.status.get().c_str());
		failures++;
		return true;
	}
	CHECK(session.isOpen.get());
	CHECK(session.buttonCount.get() == 8);
	CHECK(roll.rowLists()[0].model->columnCount() == 8);
	CHECK(roll.rowLists()[0].model->columnName(4) == "P1 A");

	/* X is bound to P1 A (button 4): held over one frame advance, it is recorded */
	int64_t changedRows = 0, appendedRows = 0;
	Connection w1 = roll.rowLists()[0].model->changed.connect([&](int64_t a, int64_t b) { changedRows += b - a + 1; });
	Connection w2 = roll.rowLists()[0].model->appended.connect([&](int64_t a, int64_t b) { appendedRows += b - a + 1; });
	session.key("X", true);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 10));
	session.key("X", false);
	CHECK(inputs.size() == 1 && inputs.at(0) == (uint64_t{1} << 4));
	CHECK(appendedRows == 1);
	CHECK(roll.currentFrame.get() == 1);

	/* a picture came out of it */
	Picture::Frame f;
	CHECK(session.screen.takeIfNewer(0, f) && f.width > 0 && f.height > 0 && f.pixels.size() == size_t(f.width) * f.height);
	/* and it saves as chimera-run writes one: a TGA, rows bottom-up */
	{
		namespace fs = std::filesystem;
		const fs::path shot = fs::temp_directory_path() / ("chimera-test-shot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tga");
		CHECK(session.run("savePicture", {shot.string()}, error).has_value());
		const std::string tga = readFile(shot);
		CHECK(tga.size() == 18 + size_t(f.width) * f.height * 4 && tga[2] == 2 && static_cast<uint8_t>(tga[16]) == 32);
		/* every row where a TGA keeps it: the picture's last row first */
		std::string rows;
		for (int32_t y = f.height - 1; y >= 0; y--)
			rows.append(reinterpret_cast<const char *>(&f.pixels[size_t(y) * f.width]), size_t(f.width) * 4);
		CHECK(tga.size() > 18 && tga.compare(18, std::string::npos, rows) == 0);
		fs::remove(shot);
	}

	/* a press on the cell flips it; a drag down the column gives the next cells the same */
	auto held = roll.run("press", {int64_t{0}, int64_t{4}}, error);
	CHECK(held && std::get<bool>(*held) == false && inputs.at(0) == 0);
	roll.run("fill", {int64_t{10}}, error);
	roll.run("press", {int64_t{2}, int64_t{7}}, error);
	roll.run("drag", {int64_t{3}, int64_t{7}}, error);
	roll.run("drag", {int64_t{4}, int64_t{7}}, error);
	CHECK(inputs.at(2) == (uint64_t{1} << 7) && inputs.at(3) == (uint64_t{1} << 7) && inputs.at(4) == (uint64_t{1} << 7));
	CHECK(changedRows >= 4);

	/* the press and its drag are one edit: one undo takes them all back,
	 * one redo does them again; an undone fill shortens the inputs again */
	CHECK(roll.canUndo.get() && !roll.canRedo.get());
	roll.run("undo", {}, error);
	CHECK(inputs.at(2) == 0 && inputs.at(3) == 0 && inputs.at(4) == 0);
	CHECK(roll.canRedo.get());
	roll.run("redo", {}, error);
	CHECK(inputs.at(2) == (uint64_t{1} << 7) && inputs.at(3) == (uint64_t{1} << 7) && inputs.at(4) == (uint64_t{1} << 7));
	CHECK(!roll.canRedo.get());
	roll.run("fill", {int64_t{25}}, error);
	CHECK(inputs.size() == 25);
	roll.run("undo", {}, error);
	CHECK(inputs.size() == 10 && roll.frameCount.get() == 10);
	roll.run("redo", {}, error);
	CHECK(inputs.size() == 25);
	roll.run("undo", {}, error);
	/* a new edit after an undo forgets what could be redone */
	roll.run("toggle", {int64_t{5}, int64_t{0}}, error);
	CHECK(!roll.canRedo.get());
	roll.run("undo", {}, error);
	CHECK(inputs.at(5) == 0 && inputs.size() == 10);

	/* playing runs at the machine's own rate and plays what the log holds */
	session.run("play", {}, error);
	CHECK(d.until([&] { return session.frame.get() >= 30; }, 10));
	/* and its sound reaches the speakers (OpenAL's null device, in the test) */
	if (session.sound.get() == "Sound on") CHECK(session.soundQueued.get() > 0);
	else std::printf("SKIP the sound check: %s\n", session.sound.get().c_str());
	session.run("pause", {}, error);
	CHECK(d.until([&] { return !session.running.get(); }, 5));
	const int64_t stopped = session.frame.get();
	d.until([] { return false; }, 0.2);
	CHECK(session.frame.get() == stopped);
	CHECK(inputs.at(3) == (uint64_t{1} << 7)); /* played, not overwritten */
	CHECK(inputs.size() >= stopped);

	/* seeking: back to a frame and forward again draws the same picture,
	 * and a seek near a stored state runs only the last frame */
	auto pictureNow = [&] {
		Picture::Frame p;
		session.screen.takeIfNewer(0, p);
		return p.pixels;
	};
	auto seek = [&](int64_t f) {
		std::string e;
		session.run("seek", {f}, e);
		const bool there = d.until([&] { return session.frame.get() == f; }, 20);
		d.until([] { return false; }, 0.05); /* let the last report land */
		return there;
	};
	const std::vector<uint32_t> atStop = pictureNow();
	CHECK(session.greenEnd.get() == stopped);
	CHECK(session.greenzoneStates.get() > 0);
	CHECK(seek(10));
	CHECK(session.frame.get() == 10 && session.lastSeekFrames.get() == 1);
	CHECK(session.greenEnd.get() == stopped); /* going back changes no input */
	CHECK(seek(stopped));
	CHECK(pictureNow() == atStop);

	/* an edit before the current frame: the states after it go, the
	 * machine goes back to it and runs on to where it was (autoRestore) */
	session.autoRestore.set(true);
	const auto right = std::find(session.buttonNames().begin(), session.buttonNames().end(), "P1 Right") - session.buttonNames().begin();
	CHECK(right < 8);
	roll.run("paint", {int64_t{5}, int64_t{25}, static_cast<int64_t>(right), true}, error);
	CHECK(d.until([&] { return session.frame.get() == stopped && session.greenEnd.get() == stopped && session.lastSeekFrames.get() > 0; }, 20));
	d.until([] { return false; }, 0.1);
	const std::vector<uint32_t> walked = pictureNow();
	CHECK(walked != atStop); /* the walker walked: the edit reached the machine */
	/* and taking the edit back gives the first picture again */
	roll.run("paint", {int64_t{5}, int64_t{25}, static_cast<int64_t>(right), false}, error);
	CHECK(d.until([&] { return session.frame.get() == stopped && session.greenEnd.get() == stopped; }, 20));
	d.until([] { return false; }, 0.1);
	CHECK(pictureNow() == atStop);
	/* without autoRestore the machine stays at the edit */
	session.autoRestore.set(false);
	roll.run("toggle", {int64_t{3}, static_cast<int64_t>(right)}, error);
	CHECK(d.until([&] { return session.frame.get() == 3; }, 20));
	CHECK(session.greenEnd.get() == 3);
	roll.run("toggle", {int64_t{3}, static_cast<int64_t>(right)}, error);
	session.autoRestore.set(true);

	/* The engine honours an edit behind the machine whoever asks next: a
	 * seek forward past it, or one frame - with no piano roll to send the
	 * machine back. What it gives equals a whole replay from frame 0. The
	 * edits span several frames: on some the game reads no input. */
	{
		const int64_t at = session.frame.get();
		std::vector<uint64_t> kept(20);
		inputs.read(0, kept);
		auto flip = [&](int64_t first, int64_t last) {
			for (int64_t f = first; f <= last; f++) inputs.set(f, inputs.at(f) ^ (uint64_t{1} << right)); /* straight into the engine's movie */
		};
		auto replayed = [&](int64_t to) {
			std::string e;
			session.run("seek", {int64_t{0}}, e);
			d.until([&] { return session.frame.get() == 0; }, 20);
			session.run("seek", {to}, e);
			d.until([&] { return session.frame.get() == to; }, 20);
			return session.readDomain("RAM");
		};
		flip(2, 9);
		session.run("seek", {at + 5}, error);
		CHECK(d.until([&] { return session.frame.get() == at + 5; }, 20));
		const std::vector<uint8_t> forward = session.readDomain("RAM");
		CHECK(replayed(at + 5) == forward);
		/* and one frame: the same frames back as they were, which matter */
		flip(2, 9);
		session.run("frameAdvance", {}, error);
		CHECK(d.until([&] { return session.frame.get() == at + 6; }, 20));
		const std::vector<uint8_t> stepped = session.readDomain("RAM");
		CHECK(replayed(at + 6) == stepped);
		for (int64_t f = 0; f < 20; f++) inputs.set(f, kept[static_cast<size_t>(f)]);
		inputs.resize(at);
		session.run("seek", {at}, error);
		CHECK(d.until([&] { return session.frame.get() == at; }, 20));
	}

	/* restart: frame 0 again, the inputs kept */
	const int64_t logged = inputs.size();
	session.run("restart", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 0 && session.isOpen.get(); }, 30));
	CHECK(inputs.size() == logged);

	/* commands a view could not reach while closed */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 10));
	CHECK(!session.run("play", {}, error) && error.find("cannot run now") != std::string::npos);
	return true;
}

/* RAM Search: after "not equal to the value at the start", the candidates are
 * exactly the addresses whose byte differs between two reads of RAM */
bool ramSearch(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	RamSearchViewModel search(d, session);
	std::string error;
	session.run("open", {std::string(package), std::string(rom)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && search.domains.get().find("RAM") != std::string::npos; }, 60));
	search.set("domain", std::string("RAM"), error);
	search.set("size", int64_t{1}, error);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 10));
	const std::vector<uint8_t> before = session.readDomain("RAM");
	CHECK(!before.empty());
	CHECK(search.run("start", {}, error).has_value());
	CHECK(d.until([&] { return search.searching.get() && search.count.get() == static_cast<int64_t>(before.size()); }, 20));

	/* the walker walks down for ten frames */
	session.key("Down", true);
	for (int i = 0; i < 10; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 11; }, 20));
	session.key("Down", false);
	const std::vector<uint8_t> after = session.readDomain("RAM");
	std::vector<std::string> changed;
	const int digits = watch::addressDigits(static_cast<int64_t>(before.size()) - 1);
	for (size_t a = 0; a < before.size() && a < after.size(); a++)
		if (before[a] != after[a]) changed.push_back(watch::hex(static_cast<int64_t>(a), digits));
	CHECK(!changed.empty());
	search.set("compare", int64_t{0}, error); /* the previous value */
	search.set("op", int64_t{5}, error);      /* not equal */
	search.run("search", {}, error);
	CHECK(d.until([&] { return search.count.get() == static_cast<int64_t>(changed.size()); }, 20));
	std::vector<std::string> listed;
	RowModel &rows = *search.rowLists()[0].model;
	for (int64_t r = 0; r < rows.rowCount(); r++)
	{
		std::string a;
		fromValue(rows.cell(r, 0), a);
		listed.push_back(a);
	}
	std::sort(listed.begin(), listed.end());
	CHECK(listed == changed);
	/* the value column is the byte now, shown each way a byte can be */
	CHECK(search.displays.get() == "Unsigned\nSigned\nHexadecimal\nBinary");
	std::string address, shownValue;
	fromValue(rows.cell(0, 0), address);
	const uint8_t byte = after[std::stoul(address, nullptr, 16)];
	fromValue(rows.cell(0, 1), shownValue);
	CHECK(shownValue == std::to_string(byte));
	search.set("display", int64_t{2}, error);
	fromValue(rows.cell(0, 1), shownValue);
	CHECK(shownValue == watch::format(byte, 1, watch::Display::Hex));
	/* a value typed as shown: in hex, the candidates holding it are kept */
	const int64_t before2 = search.count.get();
	search.set("compare", int64_t{1}, error);
	search.set("op", int64_t{0}, error);
	search.set("value", watch::format(byte, 1, watch::Display::Hex), error);
	CHECK(search.run("search", {}, error).has_value());
	CHECK(d.until([&] { return search.count.get() < before2 || search.status.get().find("0 removed") != std::string::npos; }, 20));
	d.until([] { return false; }, 0.1);
	for (int64_t r = 0; r < rows.rowCount(); r++)
	{
		fromValue(rows.cell(r, 1), shownValue);
		CHECK(shownValue == watch::format(byte, 1, watch::Display::Hex));
	}
	search.set("value", std::string("0x10"), error);
	CHECK(!search.run("search", {}, error) && error == "\"0x10\" is not a value shown as Hexadecimal");
	/* four bytes: seven ways, and a way that no longer fits goes back to the first */
	search.set("display", int64_t{3}, error);
	search.set("size", int64_t{4}, error);
	CHECK(search.displays.get().find("Float") != std::string::npos && search.display.get() == 3);
	search.set("display", int64_t{6}, error);
	search.set("size", int64_t{2}, error);
	CHECK(search.display.get() == 0);
	search.set("size", int64_t{1}, error);
	search.set("compare", int64_t{0}, error);
	search.set("op", int64_t{5}, error);
	search.run("undo", {}, error);
	CHECK(d.until([&] { return search.count.get() == static_cast<int64_t>(changed.size()); }, 20));
	/* taken back, and done again */
	CHECK(search.canUndo.get());
	search.run("undo", {}, error);
	CHECK(d.until([&] { return search.count.get() == static_cast<int64_t>(before.size()); }, 20));
	CHECK(search.canRedo.get());
	search.run("redo", {}, error);
	CHECK(d.until([&] { return search.count.get() == static_cast<int64_t>(changed.size()); }, 20));
	/* the machine closing ends the search: its memory goes with it */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !search.searching.get() && search.count.get() == 0; }, 20));
	return true;
}

/* Which settings apply: a setting naming machines belongs to them alone,
 * the machine being what the package's machineSetting says */
void settingsByMachine()
{
	json::Json decl;
	std::string error;
	CHECK(json::parse(R"([
		{"name": "machine", "type": "enum", "options": ["pc", "pc98"], "default": "pc"},
		{"name": "everywhere", "type": "bool", "default": false},
		{"name": "sound", "type": "enum", "options": ["sb", "none"], "default": "sb", "when": ["pc"]},
		{"name": "fm", "type": "bool", "default": true, "when": ["pc98"]}
	])", decl, error));
	json::Json values;
	values.kind = json::Json::Kind::Object;
	auto names = [&](const std::vector<size_t> &shown) {
		std::string out;
		for (size_t i : shown) out += decl.items[i].find("name")->text + " ";
		return out;
	};
	CHECK(names(SettingsViewModel::applicable(decl.items, "{}", values, "machine")) == "machine everywhere sound ");
	json::Json pc98;
	CHECK(json::parse(R"({"machine": "pc98"})", pc98, error));
	CHECK(names(SettingsViewModel::applicable(decl.items, "{}", pc98, "machine")) == "machine everywhere fm ");
	/* a package with no machineSetting shows them all */
	CHECK(names(SettingsViewModel::applicable(decl.items, "{}", values, "")) == "machine everywhere sound fm ");
}

/* Settings: the synthetic core declares one, the byte its RAM starts filled
 * with. Set and applied, the machine starts again with it, and its RAM says so */
bool settings(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	SettingsViewModel settings(d, session);
	std::string error;
	session.run("open", {std::string(package), std::string(rom)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && settings.shown.get() == 1; }, 60));
	RowModel &rows = *settings.rowLists()[0].model;
	std::string name, type;
	fromValue(rows.cell(0, SettingsViewModel::kName), name);
	fromValue(rows.cell(0, SettingsViewModel::kType), type);
	CHECK(name == "initFillByte" && type == "int");
	int64_t value = -1;
	fromValue(rows.cell(0, SettingsViewModel::kValue), value);
	CHECK(value == 0);
	auto filled = [&](uint8_t b) {
		const std::vector<uint8_t> ram = session.readDomain("RAM");
		return std::count(ram.begin(), ram.end(), b);
	};
	const auto zeros = filled(0);
	CHECK(zeros > 1000); /* most of the 4 KiB is never written by the game */

	/* refused: out of range, not declared, not a number */
	CHECK(!settings.run("set", {std::string("initFillByte"), int64_t{256}}, error) && error.find("goes from 0 to 255") != std::string::npos);
	CHECK(!settings.run("set", {std::string("nothing"), int64_t{1}}, error) && error.find("no setting nothing") != std::string::npos);
	CHECK(!settings.run("set", {std::string("initFillByte"), std::string("lots")}, error));

	/* set, discarded, set again, applied */
	CHECK(settings.run("set", {std::string("initFillByte"), int64_t{171}}, error).has_value());
	CHECK(settings.pending.get());
	bool edited = false;
	fromValue(rows.cell(0, SettingsViewModel::kEdited), edited);
	CHECK(edited);
	settings.run("discard", {}, error);
	CHECK(!settings.pending.get());
	settings.run("set", {std::string("initFillByte"), int64_t{171}}, error);
	const int64_t opensBefore = session.opens.get();
	settings.run("apply", {}, error);
	CHECK(d.until([&] { return session.opens.get() > opensBefore && !settings.pending.get(); }, 60));
	CHECK(settings.status.get().find("started again") != std::string::npos);
	CHECK(filled(0xAB) >= zeros); /* the machine started with its RAM filled with 171 */
	fromValue(rows.cell(0, SettingsViewModel::kValue), value);
	CHECK(value == 171);
	/* and the restart (File > Restart) keeps it */
	const int64_t opensAgain = session.opens.get();
	session.run("restart", {}, error);
	CHECK(d.until([&] { return session.opens.get() > opensAgain; }, 60));
	CHECK(filled(0xAB) >= zeros);
	/* another game starts from the package's own settings */
	const int64_t opensThird = session.opens.get();
	session.run("open", {std::string(package), std::string(rom)}, error);
	CHECK(d.until([&] { return session.opens.get() > opensThird; }, 60));
	CHECK(filled(0) == zeros);
	return true;
}

std::string readFile(const std::filesystem::path &p)
{
	std::ifstream in(p, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

/* A project: the synthetic game's winning movie, played to its end, leaves
 * the machine's memory exactly as the witness's recorded run does; saved and
 * opened again, it holds the same inputs, and an edit survives the trip. */
bool project(const char *package, const char *rom, const std::string &movies, const std::string &goldens)
{
	namespace fs = std::filesystem;
	const std::string movie = readFile(movies + "/gridWalker.win.txt");
	const std::string ram = readFile(goldens + "/gridWalker.win.ram.bin"), vram = readFile(goldens + "/gridWalker.win.vram.bin");
	if (movie.empty() || ram.empty() || vram.empty())
	{
		std::printf("SKIP the project tests: the witness's movie or goldens are missing\n");
		return false;
	}
	std::vector<std::string> entries;
	std::istringstream lines(movie);
	for (std::string l; std::getline(lines, l);)
	{
		if (!l.empty() && l.back() == '\r') l.pop_back();
		if (!l.empty() && l[0] == '|') entries.push_back(l);
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-vm-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "gridWalker.testrom");
	char sha[41];
	const std::string romBytes = readFile(rom);
	ce_sha1_hex(reinterpret_cast<const uint8_t *>(romBytes.data()), romBytes.size(), sha);
	std::string input = "[Input]\nLogKey:#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|\n";
	for (const auto &e : entries) input += e + "\n";
	input += "[/Input]\n";
	std::string escaped;
	for (char c : input) escaped += c == '\n' ? std::string("\\n") : std::string(1, c);
	{
		std::ofstream out(dir / "p.chimeraProject", std::ios::binary);
		out << "{\"title\": \"gridWalker.win\", \"core\": {\"name\": \"Synth\", \"version\": \"\", \"sha1\": \"\"}, "
			<< "\"headers\": {\"MovieVersion\": \"Chimera Project File v1.1\", \"Platform\": \"Synth\"}, "
			<< "\"files\": [{\"name\": \"gridWalker.testrom\", \"sha1\": \"" << sha << "\", \"slot\": \"rom\"}], "
			<< "\"input\": \"" << escaped << "\"}";
	}

	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	BranchesViewModel branches(d, inputs, session, roll);
	std::string error;
	auto openIt = [&](const fs::path &p) {
		std::string e;
		const int64_t was = session.opens.get();
		session.run("openProject", {(p).string(), std::string(package)}, e);
		return d.until([&] { return session.opens.get() > was && session.projectPath.get() == p.string(); }, 60);
	};
	CHECK(openIt(dir / "p.chimeraProject"));
	if (!session.isOpen.get())
	{
		std::fprintf(stderr, "FAIL: the project did not open: %s\n", session.status.get().c_str());
		failures++;
		return true;
	}
	CHECK(session.projectTitle.get() == "gridWalker.win");
	CHECK(session.recentProjects.get() == (dir / "p.chimeraProject").string());
	CHECK(roll.frameCount.get() == static_cast<int64_t>(entries.size()));
	CHECK(inputs.at(0) == 0 && inputs.at(1) == (uint64_t{1} << 1)); /* "|.D......|" is P1 Down */

	/* played to its end: the witness's memory, byte for byte */
	const int64_t end = static_cast<int64_t>(entries.size());
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	CHECK(session.readDomain("RAM") == std::vector<uint8_t>(ram.begin(), ram.end()));
	CHECK(session.readDomain("VRAM") == std::vector<uint8_t>(vram.begin(), vram.end()));
	/* the comparison can fail: without its Down presses the movie ends elsewhere
	 * (it does not notice a shift of one frame: this movie ends the same way) */
	roll.run("paint", {int64_t{0}, end - 1, int64_t{1}, false}, error);
	CHECK(d.until([&] { return session.frame.get() == end && session.greenEnd.get() == end; }, 30));
	CHECK(session.readDomain("RAM") != std::vector<uint8_t>(ram.begin(), ram.end()));
	CHECK(openIt(dir / "p.chimeraProject"));

	/* saved and opened again: the same inputs, and the same input text */
	session.run("saveProject", {(dir / "saved.chimeraProject").string()}, error);
	CHECK(d.until([&] { return session.projectPath.get() == (dir / "saved.chimeraProject").string(); }, 20));
	std::vector<uint64_t> before(entries.size()), after(entries.size());
	inputs.read(0, before);
	CHECK(openIt(dir / "saved.chimeraProject"));
	inputs.read(0, after);
	CHECK(before == after && inputs.size() == end);
	CHECK(readFile(dir / "saved.chimeraProject").find("|.D......|") != std::string::npos);

	/* File > Restart on a project: frame 0 again, the movie as edited */
	{
		const uint64_t was = inputs.at(40);
		roll.run("toggle", {int64_t{40}, int64_t{5}}, error);
		const int64_t opensBefore = session.opens.get();
		session.run("restart", {}, error);
		CHECK(d.until([&] { return session.opens.get() > opensBefore; }, 30));
		CHECK(session.isOpen.get() && session.frame.get() == 0 && session.projectPath.get() == (dir / "saved.chimeraProject").string());
		CHECK(inputs.at(40) == (was ^ (uint64_t{1} << 5)));
		roll.run("toggle", {int64_t{40}, int64_t{5}}, error);
	}

	/* markers survive the trip too */
	roll.run("addMarker", {int64_t{30}, std::string("the corner")}, error);
	roll.run("addMarker", {int64_t{12}, std::string("turn")}, error);
	CHECK(roll.markerCount.get() == 2);
	CHECK((roll.rowLists()[0].model->rowFlags(12) & PianoRollViewModel::kMarker) != 0);
	CHECK(roll.rowLists()[0].model->rowNote(30) == "the corner");
	CHECK(std::get<int64_t>(*roll.run("nextMarker", {int64_t{12}}, error)) == 30);
	CHECK(std::get<int64_t>(*roll.run("previousMarker", {int64_t{12}}, error)) == -1);
	CHECK(!roll.run("removeMarker", {int64_t{13}}, error) && error == "no marker on frame 13");
	const std::string beforeMarkers = readFile(dir / "saved.chimeraProject");
	session.run("saveProject", {std::string()}, error);
	CHECK(d.until([&] { return readFile(dir / "saved.chimeraProject") != beforeMarkers; }, 20));
	CHECK(openIt(dir / "p.chimeraProject"));
	CHECK(roll.markerCount.get() == 0); /* the other project has none */
	/* the newest first, each once */
	CHECK(session.recentProjects.get() == (dir / "p.chimeraProject").string() + "\n" + (dir / "saved.chimeraProject").string());
	CHECK(openIt(dir / "saved.chimeraProject"));
	CHECK(roll.markerCount.get() == 2 && roll.rowLists()[0].model->rowNote(12) == "turn");
	RowModel &marks = *roll.rowLists()[1].model;
	int64_t firstFrame = -1;
	fromValue(marks.cell(0, 0), firstFrame);
	CHECK(marks.rowCount() == 2 && firstFrame == 12);
	roll.run("removeMarker", {int64_t{12}}, error);
	CHECK(roll.markerCount.get() == 1);

	/* an edit survives the trip: P1 A on frame 10 */
	const std::string unedited = readFile(dir / "saved.chimeraProject");
	roll.run("toggle", {int64_t{10}, int64_t{4}}, error);
	session.run("saveProject", {std::string()}, error);
	CHECK(d.until([&] { return readFile(dir / "saved.chimeraProject") != unedited; }, 20));
	CHECK(openIt(dir / "p.chimeraProject"));
	CHECK(inputs.at(10) == before[10]);
	CHECK(openIt(dir / "saved.chimeraProject"));
	CHECK(inputs.at(10) == (before[10] | (uint64_t{1} << 4)));
	/* branches: the winning movie kept at its end, the movie spoiled, the
	 * branch loaded - and the machine is where the winning movie leaves it.
	 * With a project open a branch keeps its state in the cache folder, so
	 * loading it is a state load, not a replay. */
	const fs::path cache = dir / "cache";
	session.cacheFolder.set(cache.string());
	auto filesNamed = [&](const char *extension) {
		std::vector<fs::path> found;
		std::error_code ec;
		for (const auto &e : fs::recursive_directory_iterator(cache, ec))
			if (e.path().extension() == extension) found.push_back(e.path());
		return found;
	};
	auto shown = [&] {
		Picture::Frame f;
		session.screen.takeIfNewer(~uint64_t{0}, f);
		return f.pixels;
	};
	CHECK(openIt(dir / "p.chimeraProject"));
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	d.until([] { return false; }, 0.1);
	const std::vector<uint32_t> winPicture = shown();
	CHECK(branches.run("create", {std::string("win")}, error).has_value() && branches.count.get() == 1);
	CHECK(d.until([&] { return branches.made.get() == 1; }, 30));
	CHECK(filesNamed(".state").size() == 1 && filesNamed(".note").size() == 1);
	roll.run("addMarker", {int64_t{20}, std::string("in the branch")}, error);
	/* removed, its state file is kept while Undo may put it back */
	branches.run("remove", {int64_t{0}}, error);
	CHECK(branches.count.get() == 0 && branches.undoText.get() == "Undo Branch Removal");
	d.until([] { return false; }, 0.2);
	CHECK(filesNamed(".state").size() == 1);
	CHECK(branches.run("undo", {}, error).has_value() && branches.count.get() == 1 && branches.status.get() == "Branch Removal canceled");
	branches.run("remove", {int64_t{0}}, error);
	CHECK(branches.count.get() == 0);
	branches.run("create", {std::string("win")}, error);
	CHECK(d.until([&] { return branches.made.get() == 2; }, 30));
	/* a branch's own state file, by the name the list gives it */
	auto stateOf = [&](int64_t i) {
		std::string stem;
		fromValue(branches.rowLists()[0].model->cell(i, 4), stem);
		return stem;
	};
	CHECK(!stateOf(0).empty() && !filesNamed(".state").empty());
	const fs::path stateDir = filesNamed(".state").at(0).parent_path();
	CHECK(fs::exists(stateDir / (stateOf(0) + ".state")) && branches.status.get().find("its state kept") != std::string::npos);
	roll.run("removeMarker", {int64_t{20}}, error);
	auto spoil = [&] {
		roll.run("paint", {int64_t{0}, end - 1, int64_t{1}, false}, error);
		CHECK(d.until([&] { return session.frame.get() == end && session.greenEnd.get() == end; }, 30));
		d.until([] { return false; }, 0.1);
		CHECK(session.readDomain("RAM") != std::vector<uint8_t>(ram.begin(), ram.end()));
		CHECK(shown() != winPicture);
	};
	auto loadIt = [&](bool fromState) {
		const int64_t was = branches.loads.get();
		CHECK(branches.run("load", {int64_t{0}}, error).has_value());
		CHECK(d.until([&] { return branches.loads.get() > was && session.frame.get() == end; }, 30));
		d.until([] { return false; }, 0.1);
		CHECK(branches.loadedFromState.get() == fromState);
		/* a state load replays nothing: the states past where the movies
		 * part are not there again, as a replay would make them */
		CHECK(fromState ? session.greenEnd.get() < end : session.greenEnd.get() == end);
		CHECK(session.readDomain("RAM") == std::vector<uint8_t>(ram.begin(), ram.end()));
		CHECK(session.readDomain("VRAM") == std::vector<uint8_t>(vram.begin(), vram.end()));
		CHECK(shown() == winPicture);
	};
	spoil();
	loadIt(true);
	CHECK(roll.frameCount.get() == end && roll.markerCount.get() == 1 && roll.rowLists()[0].model->rowNote(20) == "in the branch");
	/* loaded from another frame, and played on: the session counts from
	 * the branch's frame */
	session.run("seek", {int64_t{5}}, error);
	CHECK(d.until([&] { return session.frame.get() == 5; }, 30));
	loadIt(true);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == end + 1; }, 30));
	d.until([] { return false; }, 0.1);
	CHECK(session.frame.get() == end + 1 && inputs.size() == end + 1);
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	CHECK(!branches.run("load", {int64_t{3}}, error) && error == "no branch 3");
	/* a state that does not fit is not loaded, and the branch is replayed
	 * to: a note naming another machine, then a state cut short */
	const fs::path stateFile = stateDir / (stateOf(0) + ".state"), noteFile = stateDir / (stateOf(0) + ".note");
	const std::string note = readFile(noteFile), state = readFile(stateFile);
	{
		std::string other = note;
		const size_t at = other.find("machine ");
		CHECK(at != std::string::npos);
		other.replace(at + 8, 16, "0123456789abcdef");
		std::ofstream(noteFile, std::ios::binary | std::ios::trunc) << other;
	}
	spoil();
	loadIt(false);
	std::ofstream(noteFile, std::ios::binary | std::ios::trunc) << note;
	std::ofstream(stateFile, std::ios::binary | std::ios::trunc) << state.substr(0, state.size() / 2);
	spoil();
	loadIt(false);
	CHECK(branches.status.get().find("its state could not be loaded") != std::string::npos);
	std::ofstream(stateFile, std::ios::binary | std::ios::trunc) << state;
	spoil();
	loadIt(true);
	/* kept with the project, and found again by it; what no branch has is
	 * cleared away when the project opens (as Chimera does: a copy of a
	 * project keeps its id and so shares the folder, and opening the other
	 * copy would clear this branch's state - which costs a replay, no more) */
	session.run("saveProject", {(dir / "branched.chimeraProject").string()}, error);
	CHECK(d.until([&] { return session.projectPath.get() == (dir / "branched.chimeraProject").string(); }, 20));
	{
		std::ofstream(stateFile.parent_path() / "0123456789abcdef.state") << "left behind";
		std::ofstream(stateFile.parent_path() / "0123456789abcdef.note") << "left behind";
	}
	CHECK(filesNamed(".state").size() >= 2);
	CHECK(openIt(dir / "branched.chimeraProject"));
	CHECK(branches.count.get() == 1);
	CHECK(filesNamed(".state") == std::vector<fs::path>{stateFile} && filesNamed(".note") == std::vector<fs::path>{noteFile});
	std::string branchName;
	int64_t branchFrames = 0;
	fromValue(branches.rowLists()[0].model->cell(0, 0), branchName);
	fromValue(branches.rowLists()[0].model->cell(0, 3), branchFrames);
	CHECK(branchName == "win" && branchFrames == end);
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	spoil();
	loadIt(true);

	/* The greenzone is kept with the project: saved, it is there when the
	 * project opens again, and going to the end runs from its states
	 * rather than from the start. */
	const fs::path projectCache = stateFile.parent_path().parent_path(), history = projectCache / "history.bin";
	const std::string branched = (dir / "branched.chimeraProject").string();
	/* played through from the start, so it holds the frames near the end */
	session.run("seek", {int64_t{0}}, error);
	CHECK(d.until([&] { return session.frame.get() == 0; }, 30));
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	session.run("saveProject", {std::string()}, error);
	CHECK(d.until([&] { return session.status.get().rfind("Saved " + branched, 0) == 0; }, 20));
	CHECK(openIt(branched));
	/* the machine says it is open a moment before the project's session marks itself running */
	CHECK(d.until([&] { return fs::exists(history) && fs::exists(projectCache / "session.running"); }, 10));
	CHECK(d.until([&] { return session.greenEnd.get() > 0; }, 10));
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	d.until([] { return false; }, 0.1);
	CHECK(session.lastSeekFrames.get() < end / 2);
	CHECK(session.readDomain("RAM") == std::vector<uint8_t>(ram.begin(), ram.end()));
	/* a session that did not end keeps nothing it wrote: the mark it
	 * leaves while running is found, its greenzone is not loaded and goes */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	CHECK(!fs::exists(projectCache / "session.running"));
	std::ofstream(projectCache / "session.running") << "a session that never ended";
	CHECK(openIt(branched));
	d.until([] { return false; }, 0.2);
	CHECK(session.greenEnd.get() == 0 && !fs::exists(history));
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	d.until([] { return false; }, 0.1);
	CHECK(session.lastSeekFrames.get() == end);
	CHECK(session.readDomain("RAM") == std::vector<uint8_t>(ram.begin(), ram.end()));

	/* opened with no package given: the installed core is found for it, and
	 * a cores folder without it says which core is missing */
	{
		const fs::path cores = dir / "Cores";
		fs::create_directories(cores);
		fs::copy_file(package, cores / "synth-box.chimeraCore");
		CoresViewModel installed(d);
		installed.set("coresFolder", (cores / "none").string(), error);
		session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
			[&installed] { return installed.firmwareFolders(); });
		session.run("openProject", {(dir / "p.chimeraProject").string()}, error);
		CHECK(d.until([&] { return session.status.get().find("No Synth core is installed") != std::string::npos; }, 20));
		installed.set("coresFolder", cores.string(), error);
		CHECK(installed.count.get() == 1);
		std::string coreName;
		fromValue(installed.rowLists()[0].model->cell(0, 0), coreName);
		CHECK(coreName == "Synth");
		const int64_t was = session.opens.get();
		session.run("openProject", {(dir / "p.chimeraProject").string()}, error);
		CHECK(d.until([&] { return session.opens.get() > was; }, 30));
		CHECK(session.coreName.get() == "Synth" && roll.frameCount.get() == end);
		session.setFinders({}, {});
		d.until([] { return false; }, 0.05);
	}

	/* and a project whose file is missing says which */
	fs::remove(dir / "gridWalker.testrom");
	session.run("openProject", {(dir / "p.chimeraProject").string(), std::string(package)}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	CHECK(session.status.get().find("gridWalker.testrom (not found)") != std::string::npos);
	fs::remove_all(dir);
	return true;
}

/* A game made a project, as the New Project window makes one: reopened, it
 * has the same movie, its core, its machine and every setting that applies */
bool newProject(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-new-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	session.run("open", {std::string(package), (dir / "walker.testrom").string()}, error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	session.key("Down", true);
	for (int i = 0; i < 5; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 5; }, 20));
	session.key("Down", false);
	std::vector<uint64_t> made(5);
	inputs.read(0, made);
	const fs::path project = dir / "walker.chimeraProject";
	CHECK(session.run("saveAsProject", {project.string(), std::string("down the corridor")}, error).has_value());
	CHECK(d.until([&] { return session.projectPath.get() == project.string(); }, 20));
	const std::string text = readFile(project);
	CHECK(text.find("\"Platform\"") != std::string::npos && text.find("initFillByte") != std::string::npos &&
		text.find("walker.testrom") != std::string::npos && text.find("Synth") != std::string::npos);
	/* a project open, Save As Project is Save Project As's */
	CHECK(!session.run("saveAsProject", {project.string()}, error) && error.find("a project is open") != std::string::npos);
	/* opened again in a new session (one machine at a time: the sandbox
	 * puts each at the same address) */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	InputLog inputs2;
	SessionViewModel session2(d, inputs2);
	session2.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session2.isOpen.get() && session2.projectPath.get() == project.string(); }, 60));
	std::vector<uint64_t> back(5);
	inputs2.read(0, back);
	CHECK(session2.projectTitle.get() == "down the corridor" && inputs2.size() == 5 && back == made);
	CHECK(made[0] != 0); /* Down was held */
	fs::remove_all(dir);
	return true;
}

/* Themes: built in and from the folder, one chosen; files that do not read
 * are named, colours that are not colours left out */
void themes()
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-themes-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	std::ofstream(dir / "Sea.chimeraTheme.json") << R"({"name": "Sea", "author": "A", "description": "waves\nand more", "dark": true,
		"colors": {"WindowBackground": "#102030", "WindowText": "#E0E0E0", "Selection": "#3060A0", "Bogus": "blue"}})";
	std::ofstream(dir / "Broken.chimeraTheme.json") << R"({"colors": {}})";
	std::ofstream(dir / "Light.chimeraTheme.json") << R"({"name": "Light", "colors": {"WindowBackground": "#000000"}})";
	std::ofstream(dir / "notes.json") << "{}";
	ManualDispatcher d;
	ThemesViewModel t(d);
	std::string error;
	CHECK(t.addBuiltIn(R"({"name": "Light", "dark": false, "colors": {"WindowBackground": "system:Control", "WindowText": "#000000"}})", error));
	CHECK(t.addBuiltIn(R"({"name": "Dark", "dark": true, "colors": {"WindowBackground": "#202020"}})", error));
	CHECK(!t.addBuiltIn(R"({"name": "", "colors": {}})", error) && error == "it has no name");
	t.folder.set(dir.string());
	/* the built-ins, then the folder's in name order; a file's theme by a
	 * built-in's name stays the built-in */
	CHECK(t.count.get() == 3);
	RowModel &rows = *t.rowLists()[0].model;
	std::string name, desc;
	fromValue(rows.cell(2, 0), name);
	fromValue(rows.cell(2, 2), desc);
	CHECK(name == "Sea" && desc == "waves and more");
	CHECK(t.status.get().find("Broken.chimeraTheme.json (it has no name)") != std::string::npos);
	/* Dark at first, as Chimera's default */
	CHECK(t.current.get() == "Dark" && t.dark.get() && t.colors.get().find("#202020") != std::string::npos);
	CHECK(t.run("choose", {std::string("Sea")}, error).has_value());
	CHECK(t.colors.get().find("#102030") != std::string::npos && t.colors.get().find("Bogus") == std::string::npos && t.dark.get());
	t.run("choose", {std::string("Light")}, error);
	CHECK(t.colors.get().find("system:Control") != std::string::npos && t.colors.get().find("#000000") != std::string::npos && !t.dark.get());
	CHECK(!t.run("choose", {std::string("Nope")}, error) && error == "no theme called Nope");
	fs::remove_all(dir);
}

/* Values written as Chimera's RAM Watch writes them (the whole range was
 * compared with .NET's own text; these few keep it so) */
void watchValues()
{
	using watch::Display;
	using watch::format;
	CHECK(format(0x80, 1, Display::Unsigned) == "128" && format(0x80, 1, Display::Signed) == "-128");
	CHECK(format(0x0A, 1, Display::Hex) == "0A" && format(0xA5, 1, Display::Binary) == "1010 0101");
	CHECK(format(0xABCD, 2, Display::Binary) == "1010 1011 1100 1101" && format(0xFFFF, 2, Display::Signed) == "-1");
	CHECK(format(0x0008, 2, Display::Fixed12_4) == "0.5000" && format(0xFFF8, 2, Display::Fixed12_4) == "-0.5000");
	CHECK(format(0x0000, 2, Display::Fixed12_4) == "0.0000" && format(0x1234, 2, Display::Hex) == "1234");
	CHECK(format(0xFFFFF000, 4, Display::Fixed20_12) == "-1" && format(0x00000800, 4, Display::Fixed20_12) == "0.5");
	CHECK(format(1, 4, Display::Fixed16_16) == "0.000015" && format(0xFFFFFFFF, 4, Display::Fixed16_16) == "-0.000015");
	CHECK(format(0x00000080, 4, Display::Fixed16_16) == "0.001953");
	CHECK(format(0x3DCCCCCD, 4, Display::Float) == "0.1" && format(0x4CBEBC20, 4, Display::Float) == "100000000");
	CHECK(format(0x4E6E6B28, 4, Display::Float) == "1E+09" && format(0x3727C5AC, 4, Display::Float) == "1E-05");
	CHECK(format(0x38D1B717, 4, Display::Float) == "0.0001" && format(0x80000000, 4, Display::Float) == "-0");
	CHECK(format(0x7F800000, 4, Display::Float) == "Infinity" && format(0x7FC00000, 4, Display::Float) == "NaN");
	CHECK(format(0x7F7FFFFF, 4, Display::Float) == "3.4028235E+38" && format(0x00000001, 4, Display::Float) == "1E-45");
	CHECK(format(0x12345678, 4, Display::Hex) == "12345678" && format(0x80000000, 4, Display::Signed) == "-2147483648");
	CHECK(watch::addressDigits(0xFF) == 2 && watch::addressDigits(0x100) == 4 && watch::addressDigits(0x10000) == 6);
	CHECK(watch::fits(2, Display::Fixed12_4) && !watch::fits(1, Display::Float) && !watch::fits(4, Display::Fixed12_4));
	/* and typed back, as Chimera's value boxes read them */
	uint32_t v = 0;
	CHECK(watch::parse("FF", Display::Hex, v) && v == 0xFF && watch::parse(" 7fffffff ", Display::Hex, v) && v == 0x7FFFFFFF);
	CHECK(!watch::parse("0x10", Display::Hex, v) && !watch::parse("123456789", Display::Hex, v) && !watch::parse("-1", Display::Hex, v));
	CHECK(watch::parse("-1", Display::Signed, v) && v == 0xFFFFFFFF && !watch::parse("2147483648", Display::Signed, v));
	CHECK(watch::parse("4294967295", Display::Unsigned, v) && v == 0xFFFFFFFF && !watch::parse("4294967296", Display::Unsigned, v));
	CHECK(watch::parse("10100101", Display::Binary, v) && v == 0xA5 && !watch::parse("1010 0101", Display::Binary, v));
	CHECK(watch::parse("0.5", Display::Fixed12_4, v) && v == 8 && watch::parse("1.99", Display::Fixed12_4, v) && v == 31);
	CHECK(watch::parse("-0.5", Display::Fixed20_12, v) && v == 0xFFFFF800 && watch::parse("1", Display::Fixed16_16, v) && v == 0x10000);
	CHECK(watch::parse("0.1", Display::Float, v) && v == 0x3DCCCCCD && !watch::parse("1e40", Display::Float, v));
	CHECK(!watch::parse("", Display::Unsigned, v) && !watch::parse("abc", Display::Unsigned, v) && watch::parse(" 12 ", Display::Unsigned, v) && v == 12);
}

/* RAM Watch: values read after every frame, the previous value and the
 * changes counted three ways, and Chimera's .wch files both ways */
bool ramWatch(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-watch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	RamWatchViewModel watches(d, session);
	std::string error;
	session.run("open", {std::string(package), (dir / "walker.testrom").string()}, error);
	CHECK(d.until([&] { return session.isOpen.get() && !watches.domains.get().empty(); }, 60));
	const std::string firstDomain = watches.domains.get().substr(0, watches.domains.get().find(','));
	CHECK(("," + watches.domains.get() + ",").find(",RAM,") != std::string::npos);
	/* a byte the game changes from one frame to the next */
	session.key("Down", true);
	int64_t frame = 0;
	auto step = [&] {
		session.run("frameAdvance", {}, error);
		frame++;
		CHECK(d.until([&] { return session.frame.get() == frame; }, 20));
	};
	step();
	std::vector<uint8_t> before = session.readDomain("RAM");
	step();
	std::vector<uint8_t> now = session.readDomain("RAM");
	const int64_t ramSize = static_cast<int64_t>(now.size());
	int64_t at = -1;
	for (int64_t i = 0; i + 4 <= ramSize && at < 0; i++)
		if (before[static_cast<size_t>(i)] != now[static_cast<size_t>(i)]) at = i;
	CHECK(at >= 0);
	if (at < 0) return false;
	const int64_t a2 = at & ~int64_t{1}, a4 = at & ~int64_t{3};
	CHECK(watches.run("add", {std::string("RAM"), at, int64_t{1}, int64_t{0}, false, std::string("changing byte")}, error).has_value());
	watches.run("add", {std::string("RAM"), a2, int64_t{2}, int64_t{2}, false, std::string("word, little end first")}, error);
	watches.run("add", {std::string("RAM"), a2, int64_t{2}, int64_t{2}, true, std::string("word, big end first")}, error);
	watches.run("add", {std::string("RAM"), a4, int64_t{4}, int64_t{3}, false, std::string("bits")}, error);
	watches.run("addSeparator", {std::string("below")}, error);
	watches.run("add", {std::string("RAM"), ramSize - 1, int64_t{2}, int64_t{0}, false, std::string("past the end")}, error);
	watches.run("add", {std::string("RAM"), a4, int64_t{4}, int64_t{7}, false, std::string("as a float")}, error);
	CHECK(!watches.run("add", {std::string("RAM"), at, int64_t{1}, int64_t{7}, false, std::string("")}, error));
	CHECK(!watches.run("add", {std::string("RAM"), at, int64_t{3}, int64_t{0}, false, std::string("")}, error));
	CHECK(watches.count.get() == 7 && watches.changed.get());
	auto text = [&](int64_t row, int32_t column) {
		std::string t;
		fromValue(watches.rowLists()[0].model->cell(row, column), t);
		return t;
	};
	auto number = [&](int64_t row, int32_t column) {
		int64_t n = -1;
		fromValue(watches.rowLists()[0].model->cell(row, column), n);
		return n;
	};
	auto shownAs = [&](const std::vector<uint8_t> &r) {
		const size_t i = static_cast<size_t>(at), w = static_cast<size_t>(a2), q = static_cast<size_t>(a4);
		const uint32_t little = r[w] | (r[w + 1] << 8), big = (r[w] << 8) | r[w + 1];
		const uint32_t dword = r[q] | (r[q + 1] << 8) | (r[q + 2] << 16) | (static_cast<uint32_t>(r[q + 3]) << 24);
		return text(0, 1) == std::to_string(r[i]) && text(1, 1) == watch::format(little, 2, watch::Display::Hex) &&
			   text(2, 1) == watch::format(big, 2, watch::Display::Hex) && text(3, 1) == watch::format(dword, 4, watch::Display::Binary) &&
			   text(6, 1) == watch::format(dword, 4, watch::Display::Float);
	};
	CHECK(d.until([&] { return shownAs(now); }, 10));
	CHECK(text(5, 1) == "-" && text(5, 4) == "-" && text(4, 6) == "below" && text(4, 1).empty());
	CHECK(text(0, 0) == watch::hex(at, watch::addressDigits(ramSize - 1)) && text(0, 5) == "RAM" && text(1, 8) == "Hexadecimal");
	CHECK(text(0, 2) == text(0, 1) && number(0, 3) == 0);
	/* the last frame's value as the previous one: every frame that moves it is a change */
	int64_t changes = 0;
	uint8_t value = now[static_cast<size_t>(at)], previous = value;
	for (int k = 0; k < 6; k++)
	{
		step();
		now = session.readDomain("RAM");
		previous = value;
		value = now[static_cast<size_t>(at)];
		if (value != previous) changes++;
		CHECK(d.until([&] { return shownAs(now) && text(0, 2) == std::to_string(previous) && number(0, 3) == changes; }, 10));
	}
	const int64_t diff = static_cast<int64_t>(value) - previous;
	CHECK(text(0, 4) == (diff > 0 ? "+" : "") + std::to_string(diff));
	/* the value before the last change */
	watches.set("previousType", int64_t{3}, error);
	uint8_t beforeChange = previous;
	for (int k = 0; k < 6; k++)
	{
		step();
		now = session.readDomain("RAM");
		const uint8_t read = now[static_cast<size_t>(at)];
		if (read != value)
		{
			beforeChange = value;
			value = read;
			changes++;
		}
		CHECK(d.until([&] { return shownAs(now) && text(0, 2) == std::to_string(beforeChange) && number(0, 3) == changes; }, 10));
	}
	/* the first value seen: nothing moves it, nothing is counted */
	watches.set("previousType", int64_t{0}, error);
	const std::string kept = text(0, 2);
	for (int k = 0; k < 3; k++)
	{
		step();
		now = session.readDomain("RAM");
		CHECK(d.until([&] { return shownAs(now) && text(0, 2) == kept && number(0, 3) == changes; }, 10));
	}
	watches.run("clearChangeCounts", {}, error);
	CHECK(number(0, 3) == 0);

	/* saved as Chimera saves it */
	watches.run("setOnScreen", {int64_t{0}, true}, error);
	watches.run("setOnScreen", {int64_t{6}, true}, error);
	CHECK(!watches.run("setOnScreen", {int64_t{4}, true}, error));
	/* shown over the picture, "note: value", in the list's order */
	CHECK(d.until([&] {
		return watches.onScreenText.get() == "changing byte: " + text(0, 1) + "\nas a float: " + text(6, 1);
	}, 10));
	watches.set("showOnScreen", false, error);
	CHECK(watches.onScreenText.get().empty());
	watches.set("showOnScreen", true, error);
	CHECK(watches.onScreenText.get().rfind("changing byte: ", 0) == 0);
	step();
	now = session.readDomain("RAM");
	CHECK(d.until([&] { return watches.onScreenText.get().rfind("changing byte: " + std::to_string(now[static_cast<size_t>(at)]) + "\n", 0) == 0; }, 10));
	const fs::path file = dir / "w.wch";
	CHECK(watches.run("save", {file.string()}, error).has_value() && !watches.changed.get() && watches.path.get() == file.string());
	const std::string saved = readFile(file);
#ifdef _WIN32
	const std::string nl = "\r\n";
#else
	const std::string nl = "\n";
#endif
	const std::string digits = watch::hex(at, watch::addressDigits(ramSize));
	CHECK(saved.rfind("SystemID Synth" + nl + digits + "\tb\tu\t0\tRAM\tchanging byte" + nl, 0) == 0);
	CHECK(saved.find(nl + "0\tS\t_\t1\t\tbelow" + nl) != std::string::npos);
	CHECK(saved.find(watch::hex(a2, watch::addressDigits(ramSize)) + "\tw\th\t1\tRAM\tword, big end first" + nl) != std::string::npos);
	CHECK(saved.size() > 2 * nl.size() && saved.substr(saved.size() - (nl.size() + 12 + nl.size())) == "OnScreen\t0,6" + nl + nl);
	/* and read back */
	watches.run("newList", {}, error);
	CHECK(watches.count.get() == 0 && watches.path.get().empty());
	CHECK(watches.run("open", {file.string()}, error).has_value());
	CHECK(watches.count.get() == 7 && !watches.changed.get());
	CHECK(text(0, 6) == "changing byte" && number(0, 7) == 1 && text(2, 8) == "Hexadecimal" && text(4, 6) == "below");
	bool on0 = false, on1 = true, on6 = false;
	fromValue(watches.rowLists()[0].model->cell(0, 9), on0);
	fromValue(watches.rowLists()[0].model->cell(1, 9), on1);
	fromValue(watches.rowLists()[0].model->cell(6, 9), on6);
	CHECK(on0 && !on1 && on6);
	CHECK(d.until([&] { return shownAs(now); }, 10));
	/* opened with another: appended, and the list has changed */
	CHECK(watches.run("open", {file.string(), true}, error).has_value() && watches.count.get() == 14 && watches.changed.get());
	/* another emulator's file (a count first, a place before each line, no
	 * domain), a domain this machine lacks, and a game property, which is
	 * kept as it was */
	{
		std::ofstream(dir / "other.wch", std::ios::binary) << "2\r\n00001\t0010\tw\th\t1\tfrom elsewhere\r\n00002\t0020\tb\tu\t0\tsecond\r\n";
		CHECK(watches.run("open", {(dir / "other.wch").string()}, error).has_value() && watches.count.get() == 2);
		CHECK(text(0, 0) == watch::hex(0x10, watch::addressDigits(ramSize - 1)) && text(0, 5) == firstDomain && text(0, 6) == "from elsewhere");
		CHECK(number(0, 7) == 2 && text(0, 8) == "Hexadecimal" && text(1, 6) == "second");
		std::ofstream(dir / "odd.wch", std::ios::binary)
			<< "SystemID Synth\n0010\tb\tu\t0\tNowhere\tlost domain\n0\tp\t_\t0\t\tplayer x\nOnScreen\t1\n";
		CHECK(watches.run("open", {(dir / "odd.wch").string()}, error).has_value() && watches.count.get() == 2);
		/* the game has no "player x": it reads as nothing, and is saved as it was */
		CHECK(text(0, 5) == firstDomain && text(1, 6) == "player x" && text(1, 8) == "Game property" && text(1, 1) == "-");
		CHECK(watches.run("save", {(dir / "odd-saved.wch").string()}, error).has_value());
		CHECK(readFile(dir / "odd-saved.wch").find(nl + "0\tp\t_\t0\t\tplayer x" + nl) != std::string::npos);
		CHECK(readFile(dir / "odd-saved.wch").find("OnScreen\t1" + nl) != std::string::npos);
	}
	CHECK(!watches.run("open", {(dir / "missing.wch").string()}, error) && error.find("cannot read") != std::string::npos);
	/* the game's own properties, by name: read by the engine as text, named
	 * values by their names, changes counted, saved as Chimera saves them */
	watches.run("newList", {}, error);
	watches.set("previousType", int64_t{2}, error);
	CHECK(watches.run("addProperty", {std::string("Steps")}, error).has_value());
	CHECK(watches.run("addProperty", {std::string("Status")}, error).has_value());
	CHECK(!watches.run("addProperty", {std::string("Nope")}, error) && error == "the game has no property Nope");
	auto stepsNow = [&] {
		const std::vector<uint8_t> r = session.readDomain("RAM");
		return std::to_string(r[4] | (r[5] << 8) | (r[6] << 16) | (static_cast<uint32_t>(r[7]) << 24));
	};
	CHECK(d.until([&] { return text(0, 1) == stepsNow() && text(1, 1) == "Playing"; }, 10));
	CHECK(text(0, 0) == watch::hex(4, watch::addressDigits(ramSize - 1)) && text(0, 5) == "RAM" && text(0, 8) == "Game property");
	/* a few frames each way: every frame Steps reads differently is a change */
	int64_t expectChanges = number(0, 3);
	std::string last = stepsNow();
	for (const char *key : {"Up", "Up", "Left", "Left", "Right", "Right", "Down", "Down"})
	{
		session.key(key, true);
		step();
		session.key(key, false);
		const std::string nowSteps = stepsNow();
		if (nowSteps != last) expectChanges++;
		last = nowSteps;
		CHECK(d.until([&] { return text(0, 1) == nowSteps && number(0, 3) == expectChanges; }, 10));
	}
	CHECK(expectChanges > 0);
	CHECK(!watches.run("setDisplay", {int64_t{0}, int64_t{2}}, error));
	watches.run("setOnScreen", {int64_t{1}, true}, error);
	CHECK(watches.onScreenText.get() == "Status: Playing");
	CHECK(watches.run("save", {(dir / "props.wch").string()}, error).has_value());
	CHECK(readFile(dir / "props.wch").find(nl + watch::hex(4, watch::addressDigits(ramSize)) + "\tp\tu\t0\tRAM\tSteps" + nl) != std::string::npos);
	watches.run("newList", {}, error);
	CHECK(watches.run("open", {(dir / "props.wch").string()}, error).has_value() && watches.count.get() == 2);
	CHECK(d.until([&] { return text(0, 1) == stepsNow() && text(1, 1) == "Playing"; }, 10));
	session.key("Down", false);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir);
	return true;
}

/* The hex editor: the lines on screen read after every frame, shown by
 * size and byte order, the values that changed marked; a poke written into
 * the machine; bytes found */
bool hexEditor(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	HexEditorViewModel hex(d, session);
	std::string error;
	session.run("open", {std::string(package), std::string(rom)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && !hex.domains.get().empty(); }, 60));
	hex.set("domain", std::string("RAM"), error);
	std::vector<uint8_t> ram = session.readDomain("RAM");
	const int64_t size = static_cast<int64_t>(ram.size());
	CHECK(size >= 256 && hex.lineCount.get() == (size + 15) / 16 && hex.writable.get());
	hex.set("visibleLines", int64_t{8}, error);
	RowModel &lines = *hex.rowLists()[0].model;
	auto text = [&](int64_t row, int32_t column) {
		std::string t;
		fromValue(lines.cell(row, column), t);
		return t;
	};
	auto flag = [&](int64_t row, int32_t column) {
		bool b = false;
		fromValue(lines.cell(row, column), b);
		return b;
	};
	const int digits = watch::addressDigits(size - 1);
	auto byteText = [](uint8_t b) { return watch::format(b, 1, watch::Display::Hex); };
	auto showsRam = [&](int64_t top) {
		if (lines.rowCount() != 8) return false;
		for (int64_t r = 0; r < 8; r++)
		{
			const int64_t address = (top + r) * 16;
			if (text(r, 0) != watch::hex(address, digits)) return false;
			for (int k = 0; k < 16; k++)
				if (text(r, 1 + 2 * k) != byteText(ram[static_cast<size_t>(address + k)])) return false;
		}
		return true;
	};
	CHECK(d.until([&] { ram = session.readDomain("RAM"); return showsRam(0); }, 10));
	/* the text the bytes spell */
	{
		std::string spelled;
		for (int k = 0; k < 16; k++) spelled += ram[static_cast<size_t>(k)] >= 0x20 && ram[static_cast<size_t>(k)] < 0x7F ? static_cast<char>(ram[static_cast<size_t>(k)]) : '.';
		CHECK(text(0, 33) == spelled);
	}
	/* two bytes a value, each end first */
	hex.set("size", int64_t{2}, error);
	CHECK(d.until([&] { return text(0, 1) == watch::format(ram[0] | (ram[1] << 8), 2, watch::Display::Hex); }, 10));
	CHECK(text(0, 3) == watch::format(ram[2] | (ram[3] << 8), 2, watch::Display::Hex) && text(0, 17).empty());
	hex.set("bigEndian", true, error);
	CHECK(d.until([&] { return text(0, 1) == watch::format((ram[0] << 8) | ram[1], 2, watch::Display::Hex); }, 10));
	hex.set("bigEndian", false, error);
	hex.set("size", int64_t{1}, error);
	/* scrolled */
	hex.set("topLine", int64_t{3}, error);
	CHECK(d.until([&] { return showsRam(3); }, 10));
	/* a frame: the bytes it changed are marked, and no others */
	const std::vector<uint8_t> before = session.readDomain("RAM");
	session.key("Down", true);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 20));
	const std::vector<uint8_t> atOne = session.readDomain("RAM");
	int64_t moved = -1;
	for (int64_t i = 0; i < size && moved < 0; i++)
		if (before[static_cast<size_t>(i)] != atOne[static_cast<size_t>(i)]) moved = i;
	CHECK(moved >= 0);
	if (moved < 0) return false;
	hex.run("goTo", {moved}, error);
	CHECK(hex.picked.get() == moved && moved / 16 >= hex.topLine.get() && moved / 16 < hex.topLine.get() + 8);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 2; }, 20));
	session.key("Down", false);
	const std::vector<uint8_t> atTwo = session.readDomain("RAM");
	ram = atTwo;
	CHECK(d.until([&] { return showsRam(hex.topLine.get()); }, 10));
	int64_t marked = 0;
	bool marksRight = true;
	for (int64_t r = 0; r < 8; r++)
		for (int k = 0; k < 16; k++)
		{
			const size_t a = static_cast<size_t>((hex.topLine.get() + r) * 16 + k);
			const bool differs = atOne[a] != atTwo[a];
			marksRight = marksRight && flag(r, 2 + 2 * k) == differs;
			marked += differs ? 1 : 0;
		}
	CHECK(marksRight && marked > 0);
	/* a poke: written into the machine, shown, and the states after this
	 * frame dropped */
	CHECK(hex.run("poke", {moved, std::string("AB")}, error).has_value());
	CHECK(d.until([&] { return session.readDomain("RAM")[static_cast<size_t>(moved)] == 0xAB; }, 10));
	ram = session.readDomain("RAM");
	CHECK(d.until([&] { return showsRam(hex.topLine.get()); }, 10));
	CHECK(session.greenEnd.get() <= session.frame.get());
	hex.set("size", int64_t{2}, error);
	hex.set("bigEndian", true, error);
	const int64_t at = (moved + 2) & ~int64_t{1};
	CHECK(hex.run("poke", {at, std::string("1234")}, error).has_value());
	CHECK(d.until([&] { ram = session.readDomain("RAM"); return ram[static_cast<size_t>(at)] == 0x12 && ram[static_cast<size_t>(at + 1)] == 0x34; }, 10));
	CHECK(!hex.run("poke", {at, std::string("12345")}, error) && !hex.run("poke", {at, std::string("zz")}, error));
	CHECK(!hex.run("poke", {size, std::string("00")}, error));
	hex.set("size", int64_t{1}, error);
	hex.set("bigEndian", false, error);
	/* found: the bytes poked, from the start */
	hex.set("picked", int64_t{-1}, error);
	CHECK(hex.run("find", {std::string("12 34")}, error).has_value());
	CHECK(d.until([&] { return hex.status.get().rfind("Found at", 0) == 0; }, 20));
	{
		/* the first place they are in the domain */
		int64_t expected = -1;
		for (int64_t i = 0; i + 1 < size && expected < 0; i++)
			if (ram[static_cast<size_t>(i)] == 0x12 && ram[static_cast<size_t>(i + 1)] == 0x34) expected = i;
		CHECK(hex.picked.get() == expected && hex.picked.get() / 16 >= hex.topLine.get() && hex.picked.get() / 16 < hex.topLine.get() + 8);
	}
	CHECK(!hex.run("find", {std::string("1")}, error) && !hex.run("find", {std::string("xy")}, error));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* The piano roll's editing: frames put in, taken out, cloned, cleared,
 * copied and pasted, cut off - each one step to undo - and markers that
 * move with the frames when bound to them */
bool pianoRollEditing(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	session.run("open", {std::string(package), std::string(rom)}, error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	for (int i = 0; i < 10; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 10; }, 20));
	/* frame f holds button f % 8 */
	for (int64_t f = 0; f < 10; f++) roll.run("paint", {f, f, f % 8, true}, error);
	auto masks = [&] {
		std::vector<uint64_t> m(static_cast<size_t>(inputs.size()));
		inputs.read(0, m);
		return m;
	};
	auto bit = [](int64_t b) { return uint64_t{1} << b; };
	const std::vector<uint64_t> start = masks();
	CHECK(start.size() == 10 && start[3] == bit(3) && start[9] == bit(1));
	/* the frames in view whose state the greenzone holds; those an edit
	 * took out of it, still known as run; a frame put in, never run */
	{
		using P = PianoRollViewModel;
		auto flags = [&](int64_t f) { return roll.frames().rowFlags(f); };
		roll.visibleFrames.set(12);
		roll.topFrame.set(0);
		CHECK(d.until([&] { return session.hasState(9); }, 20));
		CHECK((flags(5) & P::kGreen) && (flags(5) & P::kStated) && (flags(5) & P::kRan));
		session.autoRestore.set(false);
		roll.run("toggle", {int64_t{3}, int64_t{6}}, error);
		CHECK(d.until([&] { return session.frame.get() == 3 && !session.hasState(5); }, 20));
		CHECK(!(flags(5) & P::kGreen) && !(flags(5) & P::kStated) && (flags(5) & P::kRan));
		roll.run("insertFrames", {int64_t{4}, int64_t{1}}, error);
		CHECK(!(flags(4) & P::kRan) && (flags(6) & P::kRan));
		roll.run("undo", {}, error);
		roll.run("undo", {}, error);
		CHECK(masks() == start);
		session.autoRestore.set(true);
		session.run("seek", {int64_t{10}}, error);
		CHECK(d.until([&] { return session.frame.get() == 10; }, 20));
		roll.visibleFrames.set(0);
	}
	auto undoTo = [&](const std::vector<uint64_t> &want) {
		roll.run("undo", {}, error);
		return masks() == want;
	};
	/* put in */
	CHECK(roll.run("insertFrames", {int64_t{3}, int64_t{2}}, error).has_value());
	std::vector<uint64_t> want = start;
	want.insert(want.begin() + 3, 2, 0);
	CHECK(masks() == want && roll.frameCount.get() == 12);
	CHECK(undoTo(start) && roll.frameCount.get() == 10);
	roll.run("redo", {}, error);
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* taken out */
	CHECK(roll.run("deleteFrames", {int64_t{2}, int64_t{4}}, error).has_value());
	want = start;
	want.erase(want.begin() + 2, want.begin() + 5);
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* cloned after themselves */
	CHECK(roll.run("cloneFrames", {int64_t{1}, int64_t{2}}, error).has_value());
	want = start;
	want.insert(want.begin() + 3, {start[1], start[2]});
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* cleared */
	CHECK(roll.run("clearFrames", {int64_t{5}, int64_t{6}}, error).has_value());
	want = start;
	want[5] = want[6] = 0;
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* copied, pasted over, pasted in */
	CHECK(roll.run("copyFrames", {int64_t{0}, int64_t{2}}, error).has_value() && roll.clipboardFrames.get() == 3);
	CHECK(roll.run("pasteFrames", {int64_t{8}}, error).has_value());
	want = start;
	want[8] = start[0];
	want[9] = start[1];
	want.push_back(start[2]);
	CHECK(masks() == want);
	CHECK(undoTo(start));
	CHECK(roll.run("pasteInsertFrames", {int64_t{5}}, error).has_value());
	want = start;
	want.insert(want.begin() + 5, {start[0], start[1], start[2]});
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* cut: kept to paste, and taken out */
	CHECK(roll.run("cutFrames", {int64_t{1}, int64_t{2}}, error).has_value() && roll.clipboardFrames.get() == 2);
	want = start;
	want.erase(want.begin() + 1, want.begin() + 3);
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* cut off */
	CHECK(roll.run("truncate", {int64_t{6}}, error).has_value() && roll.frameCount.get() == 6);
	CHECK(undoTo(start) && roll.frameCount.get() == 10);
	/* refused */
	CHECK(!roll.run("deleteFrames", {int64_t{8}, int64_t{12}}, error) && error == "the frames go from 0 to 9");
	CHECK(!roll.run("insertFrames", {int64_t{11}, int64_t{1}}, error));
	/* markers stay on their frames, unless bound to the frames */
	roll.run("addMarker", {int64_t{5}, std::string("five")}, error);
	roll.run("addMarker", {int64_t{8}, std::string("eight")}, error);
	auto markerAt = [&](int64_t f) { return roll.rowLists()[0].model->rowNote(f); };
	roll.run("insertFrames", {int64_t{3}, int64_t{2}}, error);
	CHECK(markerAt(5) == "five" && markerAt(8) == "eight");
	roll.run("undo", {}, error);
	roll.set("bindMarkers", true, error);
	roll.run("insertFrames", {int64_t{3}, int64_t{2}}, error);
	CHECK(markerAt(7) == "five" && markerAt(10) == "eight" && markerAt(5).empty() && roll.markerCount.get() == 2);
	roll.run("deleteFrames", {int64_t{6}, int64_t{7}}, error);
	CHECK(markerAt(8) == "eight" && roll.markerCount.get() == 1);
	roll.run("undo", {}, error);
	CHECK(markerAt(7) == "five" && markerAt(10) == "eight");
	roll.run("undo", {}, error);
	CHECK(markerAt(5) == "five" && markerAt(8) == "eight" && masks() == start);
	/* the machine follows: the edits before it were honoured */
	CHECK(d.until([&] { return session.frame.get() == 10; }, 30));
	/* going back after an edit adds no frames, however soon the machine gets
	 * there: it follows a shorter movie to its end, and goes on to where the
	 * first edit found it once an undo gives the frames back */
	CHECK(roll.run("deleteFrames", {int64_t{2}, int64_t{4}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 7; }, 30) && masks().size() == 7);
	CHECK(undoTo(start));
	CHECK(d.until([&] { return session.frame.get() == 10; }, 30) && masks() == start);
	CHECK(roll.run("truncate", {int64_t{6}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 6; }, 30) && masks().size() == 6);
	CHECK(undoTo(start));
	CHECK(d.until([&] { return session.frame.get() == 10; }, 30) && masks() == start);
	/* and no seek cut short by an edit said it could not go somewhere */
	CHECK(session.status.get().find("Could not go") == std::string::npos);
	/* the undo history, step by step, by Chimera's names */
	{
		RowModel *history = nullptr;
		for (const auto &r : roll.rowLists())
			if (r.name == "history") history = r.model;
		CHECK(history != nullptr);
		if (history == nullptr) return false;
		auto stepName = [&](int64_t i) {
			std::string n;
			fromValue(history->cell(i, 0), n);
			return n;
		};
		auto done = [&](int64_t i) {
			bool b = false;
			fromValue(history->cell(i, 1), b);
			return b;
		};
		CHECK(roll.run("clearHistory", {}, error).has_value() && history->rowCount() == 0);
		roll.run("paint", {int64_t{2}, int64_t{2}, int64_t{4}, true}, error);
		roll.run("insertFrames", {int64_t{3}, int64_t{2}}, error);
		roll.run("deleteFrames", {int64_t{3}, int64_t{4}}, error);
		roll.run("toggle", {int64_t{2}, int64_t{4}}, error);
		CHECK(history->rowCount() == 4);
		CHECK(stepName(0) == "Set P1 A(On): 2" && stepName(1) == "Insert 2 frames: 3" && stepName(2) == "Delete 2 frames from 3-4" &&
			stepName(3) == "Toggle P1 A: 2");
		CHECK(roll.run("undoTo", {int64_t{1}}, error).has_value() && done(1) && !done(2) && !done(3) && history->rowCount() == 4);
		CHECK(stepName(2) == "Delete 2 frames from 3-4" && stepName(3) == "Toggle P1 A: 2");
		CHECK(roll.run("redoTo", {int64_t{2}}, error).has_value() && done(2) && !done(3));
		CHECK(roll.run("undoTo", {int64_t{-1}}, error).has_value() && !done(0) && masks() == start);
		CHECK(roll.run("redoTo", {int64_t{3}}, error).has_value() && done(3));
		CHECK(roll.run("clearHistoryTo", {int64_t{1}}, error).has_value() && history->rowCount() == 2 && stepName(0) == "Delete 2 frames from 3-4");
		roll.maxUndoSteps.set(1);
		CHECK(history->rowCount() == 1 && stepName(0) == "Toggle P1 A: 2");
		roll.run("undo", {}, error);
		roll.maxUndoSteps.set(1000);
		/* what is left done stays, the base the tests below start from */
		roll.run("paint", {int64_t{2}, int64_t{2}, int64_t{4}, false}, error);
		CHECK(masks() == start);
	}
	/* editing the shown columns alone (TAStudio's "allow editing hidden
	 * columns" off): the hidden column's presses stay on their frames */
	{
		const uint64_t h = bit(2);
		CHECK(roll.run("hideColumn", {session.buttonNames()[2]}, error).has_value());
		roll.editHiddenColumns.set(false);
		CHECK(roll.run("insertFrames", {int64_t{1}, int64_t{2}}, error).has_value());
		std::vector<uint64_t> want(12, 0);
		for (size_t f = 0; f < 10; f++) want[f] |= start[f] & h;
		want[0] |= start[0] & ~h;
		for (size_t f = 1; f < 10; f++) want[f + 2] |= start[f] & ~h;
		CHECK(masks() == want && roll.frameCount.get() == 12);
		CHECK(undoTo(start) && roll.frameCount.get() == 10);
		CHECK(roll.run("deleteFrames", {int64_t{3}, int64_t{4}}, error).has_value());
		want.assign(10, 0);
		for (size_t f = 0; f < 10; f++) want[f] = (start[f] & h) | (f < 3 ? start[f] & ~h : f + 2 < 10 ? start[f + 2] & ~h : 0);
		CHECK(masks() == want && roll.frameCount.get() == 10);
		CHECK(undoTo(start));
		CHECK(roll.run("clearFrames", {int64_t{1}, int64_t{3}}, error).has_value());
		want = start;
		for (size_t f = 1; f <= 3; f++) want[f] &= h;
		CHECK(masks() == want);
		CHECK(undoTo(start));
		/* cut off: the shown columns cleared, the empty frames at the end
		 * taken off; a hidden press keeps its frame */
		CHECK(roll.run("truncate", {int64_t{5}}, error).has_value());
		CHECK(masks() == std::vector<uint64_t>(start.begin(), start.begin() + 5));
		CHECK(undoTo(start) && roll.frameCount.get() == 10);
		CHECK(roll.run("truncate", {int64_t{1}}, error).has_value());
		CHECK(masks() == (std::vector<uint64_t>{start[0], 0, h}));
		CHECK(undoTo(start));
		/* recorded: a hidden button held does not go into the movie */
		{
			size_t down = 0;
			while (down < session.buttonNames().size() && session.buttonNames()[down].find("Down") == std::string::npos) down++;
			CHECK(down < session.buttonNames().size() && down != 2);
			roll.run("hideColumn", {session.buttonNames()[down]}, error);
			session.run("seek", {int64_t{10}}, error);
			CHECK(d.until([&] { return session.frame.get() == 10; }, 20));
			session.key("Down", true);
			session.run("frameAdvance", {}, error);
			CHECK(d.until([&] { return session.frame.get() == 11; }, 20));
			session.key("Down", false);
			CHECK(inputs.size() == 11 && masks()[10] == 0);
			roll.run("showColumn", {session.buttonNames()[down]}, error);
			session.key("Down", true);
			session.run("frameAdvance", {}, error);
			CHECK(d.until([&] { return session.frame.get() == 12; }, 20));
			session.key("Down", false);
			CHECK(inputs.size() == 12 && masks()[11] == bit(static_cast<int64_t>(down)));
			roll.run("truncate", {int64_t{10}}, error);
			CHECK(masks() == start);
		}
		/* hidden columns edited again when it is back on */
		roll.editHiddenColumns.set(true);
		CHECK(roll.run("clearFrames", {int64_t{2}, int64_t{2}}, error).has_value() && masks()[2] == 0);
		CHECK(undoTo(start));
		roll.run("showAllColumns", {}, error);
	}
	/* a column painted with its pattern: with Alt held, or as patternMode
	 * says; each control's own pattern */
	{
		RowModel *patterns = nullptr;
		for (const auto &r : roll.rowLists())
			if (r.name == "patterns") patterns = r.model;
		CHECK(patterns != nullptr && patterns->rowCount() == static_cast<int64_t>(session.buttonNames().size() + session.axes().size()));
		if (patterns == nullptr) return false;
		auto text = [&](int64_t row, int32_t col) {
			std::string t;
			fromValue(patterns->cell(row, col), t);
			return t;
		};
		const std::string five = session.buttonNames()[5];
		CHECK(text(5, 0) == five && text(5, 1) == "On x1, Off x1");
		auto columnFive = [&](int64_t from, int64_t to) {
			std::string bits;
			for (int64_t f = from; f <= to; f++) bits += (masks()[static_cast<size_t>(f)] >> 5 & 1) ? '1' : '0';
			return bits;
		};
		auto history = [&] {
			for (const auto &r : roll.rowLists())
				if (r.name == "history")
				{
					std::string n;
					fromValue(r.model->cell(r.model->rowCount() - 1, 0), n);
					return n;
				}
			return std::string();
		};
		/* Chimera's default: on a frame, off the next */
		CHECK(roll.run("press", {int64_t{0}, int64_t{5}, true}, error).has_value());
		roll.run("drag", {int64_t{3}, int64_t{5}}, error);
		CHECK(columnFive(0, 3) == "1010" && history() == "Paint Bool " + five + " from frame 0");
		CHECK(undoTo(start));
		/* its own: on two, off one, back to the second */
		CHECK(roll.run("setPattern", {five, std::string("On x2, Off x1"), int64_t{1}, true}, error).has_value());
		CHECK(text(5, 1) == "On x2, Off x1");
		roll.run("press", {int64_t{0}, int64_t{5}, true}, error);
		roll.run("drag", {int64_t{6}, int64_t{5}}, error);
		CHECK(columnFive(0, 6) == "1101010");
		CHECK(undoTo(start));
		/* begun on a held cell, it lets go */
		roll.run("press", {int64_t{5}, int64_t{5}, true}, error);
		roll.run("drag", {int64_t{7}, int64_t{5}}, error);
		CHECK(columnFive(4, 8) == "00000");
		CHECK(undoTo(start));
		/* always: without Alt too */
		roll.patternMode.set("always");
		roll.run("press", {int64_t{0}, int64_t{5}}, error);
		roll.run("drag", {int64_t{2}, int64_t{5}}, error);
		CHECK(columnFive(0, 2) == "110");
		CHECK(undoTo(start));
		/* autofire: on the Auto-hold columns only */
		roll.patternMode.set("autofire");
		roll.run("press", {int64_t{0}, int64_t{5}}, error);
		roll.run("drag", {int64_t{1}, int64_t{5}}, error);
		roll.run("drag", {int64_t{2}, int64_t{5}}, error);
		CHECK(columnFive(0, 2) == "111");
		CHECK(undoTo(start));
		CHECK(roll.run("toggleAutoHold", {five}, error) == Value(true));
		roll.run("press", {int64_t{0}, int64_t{5}}, error);
		roll.run("drag", {int64_t{2}, int64_t{5}}, error);
		CHECK(columnFive(0, 2) == "110");
		CHECK(undoTo(start));
		CHECK(roll.run("toggleAutoHold", {five}, error) == Value(false));
		roll.patternMode.set("never");
		/* what is not a pattern */
		CHECK(!roll.run("setPattern", {five, std::string("Maybe x2")}, error) && error == "\"Maybe\" is not On or Off");
		CHECK(!roll.run("setPattern", {five, std::string("On"), int64_t{3}}, error));
		CHECK(!roll.run("setPattern", {std::string("Nothing"), std::string("On")}, error));
		CHECK(roll.run("setPattern", {five, std::string("On x1, Off x1"), int64_t{0}, true}, error).has_value());
	}
	/* cloned a number of times, as one edit */
	CHECK(roll.run("cloneFrames", {int64_t{1}, int64_t{2}, int64_t{3}}, error).has_value() && roll.frameCount.get() == 16);
	want = start;
	for (int i = 0; i < 3; i++) want.insert(want.begin() + 3, {start[1], start[2]});
	CHECK(masks() == want);
	CHECK(undoTo(start));
	CHECK(!roll.run("cloneFrames", {int64_t{1}, int64_t{2}, int64_t{0}}, error) && error == "a number of times from 1 to 100000");
	/* the frames copied picked again, where they were */
	CHECK(roll.run("copyFrames", {int64_t{4}, int64_t{6}}, error).has_value());
	roll.run("select", {int64_t{-1}, int64_t{-1}}, error);
	CHECK(roll.run("reselectClipboard", {}, error).has_value() && roll.selectFirst.get() == 4 && roll.selectLast.get() == 6);
	/* with the machine left at an edit, back to where the edits began
	 * (Chimera's green arrow) */
	CHECK(d.until([&] { return session.frame.get() == 10; }, 30));
	session.autoRestore.set(false);
	CHECK(roll.run("clearFrames", {int64_t{2}, int64_t{2}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 2; }, 30));
	CHECK(session.run("seekToRestore", {}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 10; }, 30));
	CHECK(undoTo(start));
	session.autoRestore.set(true);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* Analog controls in the piano roll: a column each, its value shown; a
 * press picks a cell to edit, a drag paints its value, each one step */
bool pianoRollAxes(const char *slotsPackage, const char *rom)
{
	std::error_code ec;
	if (std::filesystem::file_size(slotsPackage, ec) == 0 || ec)
	{
		std::printf("SKIP analog controls: no slots package was made\n");
		return false;
	}
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	session.run("open", {std::string(slotsPackage), std::string(rom)}, error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	for (int i = 0; i < 7; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 7; }, 20));
	RowModel &rows = roll.frames();
	CHECK(session.axes().size() == 1 && session.axes()[0].min == -100 && session.axes()[0].max == 100);
	CHECK(rows.columnCount() == 9 && rows.columnName(8) == "P1 Dial" && rows.columnShortName(8) == "Dl" && rows.columnIsNumber(8) && !rows.columnIsNumber(7));
	auto dial = [&](int64_t f) {
		int64_t v = -999;
		fromValue(rows.cell(f, 8), v);
		return v;
	};
	CHECK(dial(2) == 0);
	/* picked */
	CHECK(roll.run("press", {int64_t{2}, int64_t{8}}, error).has_value());
	CHECK(roll.pickedFrame.get() == 2 && roll.pickedAxis.get() == 0 && roll.axisName.get() == "P1 Dial" && roll.axisMin.get() == -100 && roll.axisMax.get() == 100 && roll.axisValue.get() == 0);
	/* set, and refused out of range */
	CHECK(roll.run("setAxis", {int64_t{2}, int64_t{0}, int64_t{50}}, error).has_value() && dial(2) == 50 && roll.axisValue.get() == 50);
	CHECK(!roll.run("setAxis", {int64_t{2}, int64_t{0}, int64_t{101}}, error) && error == "P1 Dial goes from -100 to 100");
	/* Chimera's analog keys on the picked cell: by one, by ten, to the ends, kept in range */
	auto nudge = [&](const char *how) {
		roll.run("nudgeAxis", {std::string(how)}, error);
		return dial(2);
	};
	CHECK(nudge("up") == 51 && nudge("down10") == 41 && nudge("up10") == 51 && nudge("down") == 50);
	CHECK(nudge("max") == 100 && nudge("up") == 100 && nudge("min") == -100 && nudge("down10") == -100);
	CHECK(roll.run("setAxis", {int64_t{2}, int64_t{0}, int64_t{50}}, error).has_value() && dial(2) == 50);
	/* a drag paints the pressed cell's value, one step */
	roll.run("setAxis", {int64_t{3}, int64_t{0}, int64_t{77}}, error);
	roll.run("press", {int64_t{3}, int64_t{8}}, error);
	roll.run("drag", {int64_t{4}, int64_t{8}}, error);
	roll.run("drag", {int64_t{5}, int64_t{8}}, error);
	CHECK(dial(3) == 77 && dial(4) == 77 && dial(5) == 77 && dial(6) == 0);
	roll.run("undo", {}, error);
	CHECK(dial(3) == 77 && dial(4) == 0 && dial(5) == 0);
	roll.run("undo", {}, error);
	CHECK(dial(3) == 0 && dial(2) == 50);
	/* a button pressed on the same frame keeps the dial */
	roll.run("press", {int64_t{2}, int64_t{4}}, error);
	CHECK(dial(2) == 50 && (inputs.at(2) >> 4 & 1) != 0);
	/* frames taken out and put back keep their dials */
	roll.run("deleteFrames", {int64_t{1}, int64_t{1}}, error);
	CHECK(dial(1) == 50);
	roll.run("undo", {}, error);
	CHECK(dial(2) == 50 && dial(1) == 0);
	/* the movie carries the dial */
	CHECK(inputs.text().find("50") != std::string::npos);
	/* an analog column's Auto-hold holds the value given (chimera issue
	 * 298), or its most when none was given */
	{
		auto recorded = [&](int n) {
			const int64_t end = inputs.size();
			session.run("seek", {end}, error);
			d.until([&] { return session.frame.get() == end; }, 20);
			for (int i = 0; i < n; i++) session.run("frameAdvance", {}, error);
			d.until([&] { return session.frame.get() == end + n; }, 20);
			std::string values;
			for (int64_t f = end; f < end + n; f++) values += std::to_string(dial(f)) + " ";
			return values;
		};
		/* what Chimera asks first: the value to hold, offering the frame's */
		std::string question;
		CHECK(fromValue(roll.run("autoHoldQuestion", {std::string("P1 Dial")}, error).value_or(Value()), question) &&
			question == "Hold P1 Dial at (from -100 to 100):" && roll.autoHoldMin.get() == -100 && roll.autoHoldMax.get() == 100);
		CHECK(fromValue(roll.run("autoHoldQuestion", {std::string("P1 A")}, error).value_or(Value()), question) && question.empty());
		CHECK(!roll.run("toggleAutoHold", {std::string("P1 Dial"), int64_t{120}}, error) && error == "P1 Dial goes from -100 to 100");
		CHECK(roll.run("toggleAutoHold", {std::string("P1 Dial"), int64_t{-40}}, error) == Value(true));
		CHECK(recorded(2) == "-40 -40 ");
		roll.patternKind.set("autofire");
		CHECK(recorded(4) == "-40 0 -40 0 ");
		roll.patternKind.set("hold");
		CHECK(roll.run("toggleAutoHold", {std::string("P1 Dial")}, error) == Value(false));
		CHECK(recorded(1) == "0 ");
		CHECK(roll.run("toggleAutoHold", {std::string("P1 Dial")}, error) == Value(true));
		CHECK(recorded(1) == "-40 ");
		roll.run("toggleAutoHold", {std::string("P1 Dial")}, error);
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* Controls rebound and remembered per core; hotkeys run, rebound and let
 * go; frame advance and rewind held; branch slots */
bool controlsAndHotkeys(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	BranchesViewModel branches(d, inputs, session, roll);
	ControlsViewModel controls(d, session);
	HotkeysViewModel hotkeys(d);
	std::string error;
	session.run("open", {std::string(package), std::string(rom)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && controls.core.get() == "Synth"; }, 60));
	RowModel &buttons = *controls.rowLists()[0].model;
	auto text = [&](int64_t row, int32_t column) {
		std::string t;
		fromValue(buttons.cell(row, column), t);
		return t;
	};
	int64_t down = -1;
	for (int64_t r = 0; r < buttons.rowCount(); r++)
		if (text(r, 0) == "P1 Down") down = r;
	CHECK(down >= 0);
	if (down < 0) return false;
	const std::string packageKeys = text(down, 2);
	CHECK(text(down, 1) == packageKeys && packageKeys.find("Down") != std::string::npos);
	int64_t frame = 0;
	auto step = [&] {
		session.run("frameAdvance", {}, error);
		frame++;
		CHECK(d.until([&] { return session.frame.get() == frame; }, 20));
	};
	/* rebound: K holds Down, and Down holds nothing */
	CHECK(controls.run("bind", {down, std::string("K")}, error).has_value());
	bool changed = false;
	fromValue(buttons.cell(down, 3), changed);
	CHECK(text(down, 1) == "K" && changed);
	session.key("K", true);
	step();
	session.key("K", false);
	session.key("Down", true);
	step();
	session.key("Down", false);
	CHECK(inputs.at(0) == (uint64_t{1} << down) && inputs.at(1) == 0);
	{
		json::Json all;
		CHECK(json::parse(controls.saved.get(), all, error) && all.find("Synth") != nullptr &&
			  all.find("Synth")->find("P1 Down") != nullptr && all.find("Synth")->find("P1 Down")->text == "K");
	}
	/* remembered when the machine starts again */
	session.run("restart", {}, error);
	CHECK(d.until([&] { return session.opens.get() == 2; }, 60));
	CHECK(session.bindingTexts()[static_cast<size_t>(down)] == "K");
	frame = session.frame.get();
	/* one more key, then back to the package's */
	controls.run("addKey", {down, std::string("J")}, error);
	CHECK(text(down, 1) == "K, J");
	controls.run("addKey", {down, std::string("J")}, error);
	CHECK(text(down, 1) == "K, J");
	controls.run("reset", {down}, error);
	CHECK(text(down, 1) == packageKeys && controls.saved.get() == "{}");
	controls.run("bind", {down, std::string("")}, error);
	CHECK(text(down, 1).empty());
	controls.run("resetAll", {}, error);
	CHECK(text(down, 1) == packageKeys && controls.saved.get() == "{}");
	CHECK(!controls.run("bind", {int64_t{99}, std::string("K")}, error) && error == "no button 99");

	/* hotkeys: run, repeat ignored, let go, rebound, back */
	int downs = 0, ups = 0;
	hotkeys.add("General", "Frame Advance", "F", [&](bool isDown) { (isDown ? downs : ups)++; });
	hotkeys.add("General", "Rewind", "Shift+R", [&](bool) {});
	CHECK(hotkeys.key("F", true) && downs == 1);
	CHECK(!hotkeys.key("F", true) && downs == 1);
	CHECK(hotkeys.key("F", false) && ups == 1);
	CHECK(!hotkeys.key("R", true) && !hotkeys.key("Shift+F", true));
	/* every action's keys, for the menus to show */
	CHECK(hotkeys.bound.get() == "{\"Frame Advance\": \"F\", \"Rewind\": \"Shift+R\"}");
	CHECK(hotkeys.run("bind", {std::string("Frame Advance"), std::string("G")}, error).has_value());
	CHECK(hotkeys.bound.get() == "{\"Frame Advance\": \"G\", \"Rewind\": \"Shift+R\"}");
	CHECK(!hotkeys.key("F", true) && hotkeys.key("G", true) && downs == 2);
	hotkeys.key("G", false);
	CHECK(hotkeys.saved.get().find("\"G\"") != std::string::npos);
	/* a key let go after its action was rebound still lets the action go */
	hotkeys.key("G", true);
	hotkeys.run("reset", {std::string("Frame Advance")}, error);
	CHECK(hotkeys.key("G", false) && ups == 3);
	CHECK(hotkeys.saved.get() == "{}" && hotkeys.key("F", true));
	hotkeys.key("F", false);
	CHECK(!hotkeys.run("bind", {std::string("Fly"), std::string("Z")}, error) && error == "no action Fly");
	const int before = downs;
	hotkeys.run("run", {std::string("Frame Advance")}, error);
	CHECK(downs == before + 1);

	/* frame advance held: a frame, then after half a second it runs on until let go */
	const int64_t start = session.frame.get();
	session.holdFrameAdvance(true);
	CHECK(d.until([&] { return session.frame.get() == start + 1; }, 10));
	d.until([] { return false; }, 0.25);
	CHECK(session.frame.get() == start + 1 && !session.running.get());
	CHECK(d.until([&] { return session.running.get() && session.frame.get() > start + 3; }, 5));
	session.holdFrameAdvance(false);
	CHECK(d.until([&] { return !session.running.get(); }, 5));
	d.until([] { return false; }, 0.2);
	const int64_t stopped = session.frame.get();
	d.until([] { return false; }, 0.3);
	CHECK(session.frame.get() == stopped);
	/* rewind held: a frame back at once, then on back */
	session.holdRewind(true);
	CHECK(d.until([&] { return session.frame.get() == stopped - 1; }, 10));
	CHECK(d.until([&] { return session.frame.get() < stopped - 3; }, 5));
	session.holdRewind(false);
	d.until([] { return false; }, 0.2);
	const int64_t back = session.frame.get();
	d.until([] { return false; }, 0.3);
	CHECK(session.frame.get() == back && back >= 0);
	/* fast forward: faster than the machine's own rate while held */
	session.run("play", {}, error);
	session.holdFastForward(true);
	const int64_t fastFrom = session.frame.get();
	d.until([] { return false; }, 0.5);
	session.holdFastForward(false);
	session.run("pause", {}, error);
	CHECK(session.frame.get() - fastFrom > 45);

	/* branch slots: saved into, over, and a new one past the last */
	CHECK(branches.run("saveSlot", {int64_t{0}}, error).has_value() && branches.count.get() == 1);
	CHECK(d.until([&] { return branches.made.get() == 1; }, 20));
	session.run("frameAdvance", {}, error);
	const int64_t later = session.frame.get() + 1;
	CHECK(d.until([&] { return session.frame.get() == later; }, 20));
	CHECK(branches.run("saveSlot", {int64_t{0}}, error).has_value() && branches.count.get() == 1);
	CHECK(d.until([&] { return branches.made.get() == 2; }, 20));
	int64_t slotFrame = -1;
	std::string slotName;
	fromValue(branches.rowLists()[0].model->cell(0, 1), slotFrame);
	fromValue(branches.rowLists()[0].model->cell(0, 0), slotName);
	CHECK(slotFrame == later && slotName == "Branch 1");
	CHECK(branches.run("saveSlot", {int64_t{5}}, error).has_value() && branches.count.get() == 2);
	CHECK(d.until([&] { return branches.made.get() == 3; }, 20));
	/* Chimera's branch undo: an update taken back puts the old one back */
	auto branchFrame = [&](int64_t i) {
		int64_t f = -1;
		fromValue(branches.rowLists()[0].model->cell(i, 1), f);
		return f;
	};
	auto branchName = [&](int64_t i) {
		std::string n;
		fromValue(branches.rowLists()[0].model->cell(i, 0), n);
		return n;
	};
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == later + 1; }, 20));
	CHECK(branches.run("saveSlot", {int64_t{0}}, error).has_value() && branches.undoText.get() == "Undo Branch Update");
	CHECK(d.until([&] { return branches.made.get() == 4 && branchFrame(0) == later + 1; }, 20));
	CHECK(branches.run("undo", {}, error).has_value() && branchFrame(0) == later && branches.status.get() == "Branch Update canceled");
	CHECK(!branches.run("undo", {}, error) && error == "nothing to undo");
	/* a text edit, and a removal, taken back */
	CHECK(branches.run("rename", {int64_t{0}, std::string("renamed")}, error).has_value() && branchName(0) == "renamed");
	CHECK(branches.run("undo", {}, error).has_value() && branchName(0) == "Branch 1");
	CHECK(branches.run("remove", {int64_t{1}}, error).has_value() && branches.count.get() == 1);
	CHECK(branches.run("undo", {}, error).has_value() && branches.count.get() == 2 && branchName(1) == "Branch 2");
	/* Jump To: the machine on the branch's frame, the movie left as it is */
	const int64_t length = inputs.size();
	CHECK(branches.run("jumpTo", {int64_t{0}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == later; }, 20) && inputs.size() == length);
	/* a load taken back: the run as it was before it */
	session.run("seek", {int64_t{2}}, error);
	CHECK(d.until([&] { return session.frame.get() == 2; }, 20));
	roll.run("paint", {int64_t{1}, int64_t{1}, int64_t{6}, true}, error);
	const std::string beforeLoad = inputs.text();
	const int64_t loads = branches.loads.get();
	CHECK(branches.run("load", {int64_t{0}}, error).has_value() && branches.undoText.get() == "Undo Branch Load");
	CHECK(d.until([&] { return branches.loads.get() == loads + 1 && session.frame.get() == later; }, 20));
	CHECK(inputs.text() != beforeLoad);
	CHECK(branches.run("undo", {}, error).has_value());
	CHECK(d.until([&] { return branches.status.get() == "Branch Load canceled"; }, 20));
	CHECK(session.frame.get() == 2 && inputs.text() == beforeLoad);
	/* the hotkeys' Delete Branch takes the one picked */
	branches.selected.set(1);
	CHECK(branches.run("remove", {branches.selected.get()}, error).has_value() && branches.count.get() == 1 && branches.selected.get() == 0);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* The Greenzone box: read from TAStudio's part of the project, given to
 * the engine, saved back without disturbing the rest of that part */
bool greenzoneChoice(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-gzbox-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	std::string error;
	session.run("open", {std::string(package), (dir / "walker.testrom").string()}, error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	const fs::path project = dir / "g.chimeraProject";
	session.run("saveAsProject", {project.string()}, error);
	CHECK(d.until([&] { return session.projectPath.get() == project.string(); }, 20));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* a part made here carries the type Chimera's TAStudio reads it as, first */
	{
		json::Json p;
		CHECK(json::parse(readFile(project), p, error));
		const json::Json *o = p.find("tastudio") != nullptr ? p.find("tastudio")->find("o") : nullptr;
		CHECK(o != nullptr && !o->members.empty() && o->members.front().first == "$type" &&
			  o->members.front().second.text == "Chimera.Client.GUI.TAStudio+MovieClientSettings, Chimera.Client.GUI" &&
			  o->find("GreenzonePeriod") != nullptr && o->find("GreenzonePeriod")->number() == 1);
	}
	/* TAStudio's part as Chimera writes it, with a choice of 5 and another setting */
	{
		json::Json p;
		CHECK(json::parse(readFile(project), p, error));
		json::Json o, tastudio;
		o.kind = tastudio.kind = json::Json::Kind::Object;
		o.members.emplace_back("HorizontalOrientation", json::Json::boolean_(true));
		o.members.emplace_back("GreenzonePeriod", json::Json::integer(5));
		tastudio.members.emplace_back("o", o);
		/* in place of the part the save wrote */
		bool replaced = false;
		for (auto &[k, v] : p.members)
			if (k == "tastudio")
			{
				v = tastudio;
				replaced = true;
			}
		if (!replaced) p.members.emplace_back("tastudio", tastudio);
		std::ofstream(project, std::ios::binary | std::ios::trunc) << json::write(p);
	}
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && session.greenzonePeriod.get() == 5; }, 60));
	auto advance = [&](int n) {
		const int64_t to = session.frame.get() + n;
		for (int i = 0; i < n; i++) session.run("frameAdvance", {}, error);
		CHECK(d.until([&] { return session.frame.get() == to; }, 30));
		d.until([] { return false; }, 0.1);
	};
	/* one in five: twenty frames store few states; every frame: many more */
	const int64_t s0 = session.greenzoneStates.get();
	advance(20);
	const int64_t fifths = session.greenzoneStates.get() - s0;
	session.greenzonePeriod.set(1);
	advance(20);
	const int64_t every = session.greenzoneStates.get() - s0 - fifths;
	CHECK(fifths <= 6 && every >= 15);
	/* none: nothing more is stored */
	session.greenzonePeriod.set(0);
	const int64_t before = session.greenzoneStates.get();
	advance(10);
	CHECK(session.greenzoneStates.get() == before);
	/* saved into TAStudio's part, the rest of it as it was */
	session.run("saveProject", {std::string()}, error);
	CHECK(d.until([&] { return session.status.get().rfind("Saved", 0) == 0; }, 20));
	{
		json::Json p;
		CHECK(json::parse(readFile(project), p, error));
		const json::Json *o = p.find("tastudio") != nullptr ? p.find("tastudio")->find("o") : nullptr;
		CHECK(o != nullptr && o->find("GreenzonePeriod") != nullptr && o->find("GreenzonePeriod")->number() == 0 &&
			  o->find("HorizontalOrientation") != nullptr && o->find("HorizontalOrientation")->boolean);
	}
	/* cycled: every frame, every N, none */
	session.run("cycleGreenzone", {}, error);
	CHECK(session.greenzonePeriod.get() == 1);
	session.greenzoneEvery.set(4);
	session.run("cycleGreenzone", {}, error);
	CHECK(session.greenzonePeriod.get() == 4);
	session.run("cycleGreenzone", {}, error);
	CHECK(session.greenzonePeriod.get() == 0);
	/* a game alone starts on every frame */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	session.run("open", {std::string(package), (dir / "walker.testrom").string()}, error);
	CHECK(d.until([&] { return session.isOpen.get() && session.greenzonePeriod.get() == 1; }, 60));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir);
	return true;
}

/* Rerecords, counted as Chimera's TAStudio counts them: an edit over frames
 * the machine has stored states past counts one, a stroke counts once
 * however many cells it crosses, frames added at the end count none, a
 * branch loaded over the movie counts one; the count is kept with the
 * project and over a restart. */
bool rerecordCount(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-rerecords-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	BranchesViewModel branches(d, inputs, session, roll);
	std::string error;
	session.run("open", {std::string(package), (dir / "walker.testrom").string()}, error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	for (int i = 0; i < 10; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 10; }, 20));
	auto settle = [&] { d.until([] { return false; }, 0.2); };
	settle();
	CHECK(session.rerecords.get() == 0);
	/* an edit over played frames */
	roll.run("toggle", {int64_t{2}, int64_t{4}}, error);
	CHECK(d.until([&] { return session.rerecords.get() == 1 && session.frame.get() == 10; }, 20));
	/* a stroke: pressed on one cell, dragged over three more */
	roll.run("press", {int64_t{3}, int64_t{5}}, error);
	for (int64_t f = 4; f <= 6; f++)
	{
		settle();
		roll.run("drag", {f, int64_t{5}}, error);
	}
	settle();
	CHECK(session.rerecords.get() == 2);
	/* frames added at the end redo nothing */
	roll.run("fill", {int64_t{20}}, error);
	roll.run("paint", {int64_t{15}, int64_t{18}, int64_t{1}, true}, error);
	settle();
	CHECK(session.rerecords.get() == 2);
	/* undoing an edit over played frames is one */
	roll.run("undo", {}, error);
	roll.run("undo", {}, error);
	roll.run("undo", {}, error);
	settle();
	CHECK(session.rerecords.get() == 3);
	/* a branch loaded over the movie */
	CHECK(branches.run("create", {std::string("b")}, error).has_value());
	CHECK(d.until([&] { return branches.made.get() == 1; }, 20));
	roll.run("toggle", {int64_t{1}, int64_t{0}}, error);
	CHECK(d.until([&] { return session.rerecords.get() == 4 && session.frame.get() == 10; }, 20));
	branches.run("load", {int64_t{0}}, error);
	CHECK(d.until([&] { return branches.loads.get() == 1; }, 20));
	settle();
	CHECK(session.rerecords.get() == 5);
	/* kept over a restart, and with the project */
	session.run("restart", {}, error);
	CHECK(d.until([&] { return session.opens.get() == 2; }, 60));
	settle();
	CHECK(session.rerecords.get() == 5);
	const fs::path project = dir / "walker.chimeraProject";
	session.run("saveAsProject", {project.string()}, error);
	CHECK(d.until([&] { return session.projectPath.get() == project.string(); }, 20));
	{
		const std::string text = readFile(project);
		const size_t at = text.find("\"rerecords\"");
		CHECK(at != std::string::npos && std::atoi(text.c_str() + text.find(':', at) + 1) == 5);
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == project.string(); }, 60));
	settle();
	CHECK(session.rerecords.get() == 5);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir);
	return true;
}

/* New Project, as Chimera's window makes one: a core, its machine, files
 * in the slots it declares, its settings, its firmware; made, then opened */
std::string lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

bool newProjectWizard(const char *package, const char *rom, const char *slotsPackage)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(slotsPackage, ec) == 0 || ec)
	{
		std::printf("SKIP New Project: no slots package was made\n");
		return false;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-wizard-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "Cores");
	fs::create_directories(dir / "Firmware");
	fs::copy_file(package, dir / "Cores" / "synth-box.chimeraCore");
	fs::copy_file(slotsPackage, dir / "Cores" / "synth-slots.chimeraCore");
	const std::string game = (dir / "walker.testrom").string();
	fs::copy_file(rom, game);
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", (dir / "Cores").string(), error);
	installed.set("firmwareFolder", (dir / "Firmware").string(), error);
	session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
		[&installed] { return installed.firmwareFolders(); });
	NewProjectViewModel wizard(d, session, installed);
	ProjectDetailsViewModel details(d, session);
	auto cell = [&](const char *rowsName, int64_t row, int32_t column) {
		for (const auto &r : wizard.rowLists())
			if (r.name == rowsName) return r.model->cell(row, column);
		return Value{};
	};
	auto text = [&](const char *rowsName, int64_t row, int32_t column) {
		std::string t;
		fromValue(cell(rowsName, row, column), t);
		return t;
	};
	auto rowCount = [&](const char *rowsName) {
		for (const auto &r : wizard.rowLists())
			if (r.name == rowsName) return r.model->rowCount();
		return int64_t{-1};
	};
	auto settingNames = [&] {
		std::string out;
		for (int64_t r = 0; r < rowCount("settings"); r++) out += text("settings", r, 0) + " ";
		return out;
	};
	CHECK(wizard.coreNames.get() == "Synth\nSynth Slots");
	/* Chimera's list: a line a package, its version, its systems; only
	 * emulators here, so asking for games lists them still */
	CHECK(std::regex_match(wizard.coreLines.get(),
		std::regex("Synth  [0-9a-f]{8}( local)?  -  Synthetic Witness\nSynth Slots  [0-9a-f]{8}( local)?  -  Synthetic Witness, SynthB")));
	CHECK(wizard.coreLine.get() == -1 && wizard.kind.get() == "emulators");
	CHECK(wizard.run("chooseKind", {std::string("games")}, error).has_value() && wizard.coreLines.get().find("Synth Slots") != std::string::npos);
	CHECK(wizard.run("chooseKind", {std::string("emulators")}, error).has_value());
	CHECK(wizard.problems.get() == "Choose a core" && !wizard.ready.get());
	CHECK(!wizard.run("addFile", {std::string("rom"), game}, error) && error == "choose a core first");

	/* a package of one kind of file: the game */
	CHECK(wizard.run("chooseCore", {std::string("Synth")}, error).has_value());
	CHECK(rowCount("slots") == 1 && text("slots", 0, 0) == "rom" && text("slots", 0, 4) == ".testrom" && text("slots", 0, 9) == "*.testrom");
	CHECK(wizard.problems.get() == "Game: a file is needed" && wizard.machines.get().empty());
	std::ofstream(dir / "notes.txt") << "not a game";
	CHECK(!wizard.run("addFile", {std::string("rom"), (dir / "notes.txt").string()}, error) && error == "notes.txt: Game takes a .testrom file");
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && wizard.ready.get() && wizard.problems.get().empty());
	CHECK(!wizard.run("addFile", {std::string("rom"), game}, error) && error == "Game takes 1 file at most");
	CHECK(text("slots", 0, 7) == "walker.testrom");
	CHECK(settingNames() == "initFillByte ");
	/* a core that declares no renderer draws one way */
	CHECK(wizard.renderers.get() == "Software (Native)" && wizard.renderer.get() == 0);
	CHECK(!wizard.run("set", {std::string("initFillByte"), int64_t{300}}, error) && error == "initFillByte goes from 0 to 255");
	CHECK(wizard.run("set", {std::string("initFillByte"), int64_t{7}}, error).has_value());
	wizard.set("title", std::string("down the corridor"), error);
	const fs::path plain = dir / "walker.chimeraProject";
	CHECK(wizard.run("create", {plain.string()}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == plain.string(); }, 60));
	{
		const std::string project = readFile(plain);
		CHECK(std::regex_search(project, std::regex("\"initFillByte\"\\s*:\\s*7[^0-9]")));
		CHECK(project.find("down the corridor") != std::string::npos && project.find("walker.testrom") != std::string::npos);
		CHECK(project.find("\"Platform\"") != std::string::npos);
	}
	CHECK(session.projectTitle.get() == "down the corridor" && session.coreName.get() == "Synth");
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));

	/* two boards: each its own kinds of file, settings and firmware */
	CHECK(wizard.run("chooseCore", {std::string("Synth Slots")}, error).has_value());
	CHECK(wizard.machines.get() == "Board A\nBoard B" && wizard.machine.get() == 0);
	bool exposed = false;
	/* presets: the ones for this board, written as typed in, the board and
	 * a name no setting has left out and named */
	CHECK(rowCount("presets") == 1 && text("presets", 0, 1) == "Full fill");
	CHECK(wizard.run("applyPreset", {std::string("full")}, error).has_value());
	CHECK(wizard.status.get() == "Preset Full fill written below; not for this machine: nope" && wizard.machine.get() == 0);
	/* still board A: its presets, its cartridge slot */
	CHECK(rowCount("presets") == 1);
	fromValue(cell("slots", 0, 6), exposed);
	CHECK(exposed);
	{
		int64_t v = -1;
		for (int64_t r = 0; r < rowCount("settings"); r++)
			if (text("settings", r, 0) == "initFillByte") fromValue(cell("settings", r, 3), v);
		CHECK(v == 255);
	}
	CHECK(!wizard.run("applyPreset", {std::string("fast")}, error) && error == "no preset fast for this machine");
	CHECK(wizard.problems.get() == "One of these is needed: Cartridge, Disks");
	fromValue(cell("slots", 0, 6), exposed);
	CHECK(exposed && rowCount("firmware") == 0);
	CHECK(settingNames() == "initFillByte port ");
	/* the renderer is asked beside the machine, named for what it is; a
	 * hardware one says what it costs */
	CHECK(wizard.renderers.get() == "Software (Native)\nHardware (OpenGL)" && wizard.renderer.get() == 0 && wizard.rendererCaveat.get().empty());
	CHECK(wizard.run("chooseRenderer", {int64_t{1}}, error).has_value() && wizard.renderer.get() == 1);
	CHECK(wizard.rendererCaveat.get().rfind("Hardware renderers use your PC's GPU: fast, but the picture is drawn outside the savestate", 0) == 0);
	CHECK(!wizard.run("chooseRenderer", {int64_t{2}}, error) && error == "no renderer 2");
	CHECK(wizard.run("chooseRenderer", {int64_t{0}}, error).has_value() && wizard.rendererCaveat.get().empty());
	/* a board narrows what a setting offers and starts it on (board A: the
	 * mouse by default; board B: no mouse, and nothing by default) */
	auto portCell = [&](int32_t column) {
		for (int64_t r = 0; r < rowCount("settings"); r++)
			if (text("settings", r, 0) == "port") return text("settings", r, column);
		return std::string("no port");
	};
	CHECK(portCell(3) == "mouse" && portCell(4) == "pad\nmouse\nnone");
	/* the disk slot takes the kinds of disk this board claims */
	CHECK(text("slots", 1, 4) == ".dsk" && text("slots", 1, 9) == "*.dsk");
	CHECK(wizard.run("chooseMachine", {int64_t{1}}, error).has_value() && wizard.machine.get() == 1);
	CHECK(text("slots", 1, 4) == ".cue .img");
	std::ofstream(dir / "disk3.dsk") << "board A's kind of disk";
	CHECK(!wizard.run("addFile", {std::string("disk"), (dir / "disk3.dsk").string()}, error) &&
		error == "disk3.dsk: Disks takes files of these kinds: .cue .img");
	fromValue(cell("slots", 0, 6), exposed);
	CHECK(!exposed && settingNames() == "initFillByte diskSpeed port ");
	CHECK(portCell(3) == "none" && portCell(4) == "pad\nnone");
	CHECK(!wizard.run("set", {std::string("port"), std::string("mouse")}, error) && error == "mouse is not one of port's options");
	CHECK(rowCount("presets") == 2 && wizard.run("applyPreset", {std::string("fast")}, error).has_value());
	{
		int64_t v = -1;
		for (int64_t r = 0; r < rowCount("settings"); r++)
			if (text("settings", r, 0) == "diskSpeed") fromValue(cell("settings", r, 3), v);
		CHECK(v == 4);
	}
	CHECK(!wizard.run("addFile", {std::string("rom"), game}, error) && error == "Cartridge does not go with the files or the machine chosen");
	CHECK(rowCount("firmware") == 1 && text("firmware", 0, 1) == "Board B BIOS");
	CHECK(wizard.problems.get() == "One of these is needed: Disks\nBoard B BIOS: not found in the firmware folder; choose its file");
	/* a cue whose track is not beside it is said at once; names a slot asks for */
	std::ofstream(dir / "disk1.cue") << "FILE \"disk1.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n";
	CHECK(!wizard.run("addFile", {std::string("disk"), (dir / "disk1.cue").string()}, error) && error == "disk1.cue names disk1.bin, which is not beside it");
	std::ofstream(dir / "disk1.bin") << "the first disk's track";
	CHECK(wizard.run("addFile", {std::string("disk"), (dir / "disk1.cue").string()}, error).has_value());
	std::ofstream(dir / "game.img") << "a disk by another name";
	CHECK(!wizard.run("addFile", {std::string("disk"), (dir / "game.img").string()}, error) && error == "game.img: A disk is called disk, a number and .cue, .img or .dsk.");
	std::ofstream(dir / "disk2.img") << "the second disk";
	CHECK(wizard.run("addFile", {std::string("disk"), (dir / "disk2.img").string()}, error).has_value());
	CHECK(wizard.run("moveFile", {std::string("disk"), int64_t{1}, int64_t{0}}, error).has_value() && text("slots", 1, 7) == "disk2.img\ndisk1.cue");
	CHECK(wizard.run("set", {std::string("diskSpeed"), int64_t{3}}, error).has_value());
	CHECK(!wizard.run("set", {std::string("board"), std::string("a")}, error));
	/* the core is asked about the game with what is chosen so far: the slot
	 * map, the other files and a cue's tracks, the settings */
	CHECK(wizard.run("moveFile", {std::string("disk"), int64_t{1}, int64_t{0}}, error).has_value());
	CHECK(wizard.suggestion.get().find(" It sees 2 disks and the track of disk1.cue, at disk speed 3.") != std::string::npos);
	CHECK(wizard.run("moveFile", {std::string("disk"), int64_t{1}, int64_t{0}}, error).has_value() && text("slots", 1, 7) == "disk2.img\ndisk1.cue");
	/* the firmware found by scanning a folder: by the name it declares,
	 * below the folder only when asked; a folder that has it becomes the
	 * firmware folder when the one set is not there */
	fs::create_directories(dir / "scan" / "deeper");
	std::ofstream(dir / "scan" / "deeper" / "boardb.bin") << "a board B BIOS, by its name";
	installed.set("firmwareFolder", (dir / "nowhere").string(), error);
	CHECK(wizard.run("scanFolder", {(dir / "scan").string(), false}, error).has_value());
	CHECK(wizard.status.get() == "Scanned 0 files; nothing new matched" && !wizard.ready.get());
	CHECK(wizard.run("scanFolder", {(dir / "scan").string(), true}, error).has_value());
	CHECK(wizard.status.get() == "Scanned 1 file; 1 requirement resolved" && wizard.ready.get());
	CHECK(text("firmware", 0, 2) == (dir / "scan" / "deeper" / "boardb.bin").string() && text("firmware", 0, 3) == "chosen");
	CHECK(installed.firmwareFolder.get() == (dir / "scan").string());
	installed.set("firmwareFolder", (dir / "Firmware").string(), error);
	/* the folder scanned is looked in from now on, as Chimera's is: what
	 * was chosen there, forgotten, is found there again */
	const std::string scanned = (dir / "scan" / "deeper" / "boardb.bin").string();
	wizard.run("forgetFirmware", {std::string("bios")}, error);
	CHECK(wizard.ready.get() && text("firmware", 0, 2) == scanned && text("firmware", 0, 3) != "chosen");
	/* the firmware: chosen by hand, forgotten, then found in the folder */
	std::ofstream(dir / "mybios.bin") << "a board B BIOS";
	CHECK(wizard.run("chooseFirmware", {std::string("bios"), (dir / "mybios.bin").string()}, error).has_value());
	CHECK(text("firmware", 0, 2) == (dir / "mybios.bin").string() && text("firmware", 0, 3) == "chosen" && wizard.ready.get());
	wizard.run("forgetFirmware", {std::string("bios")}, error);
	CHECK(text("firmware", 0, 2) == scanned);
	wizard.run("chooseFirmware", {std::string("bios"), (dir / "mybios.bin").string()}, error);
	const fs::path boardB = dir / "boardb.chimeraProject";
	CHECK(wizard.run("create", {boardB.string(), false}, error).has_value());
	CHECK(d.until([&] { return wizard.status.get() == "Made " + boardB.string(); }, 30));
	{
		const std::string project = readFile(boardB);
		char sha[41];
		uint64_t len = 0;
		ce_sha1_file((dir / "mybios.bin").string().c_str(), sha, &len);
		CHECK(project.find("SynthB") != std::string::npos && std::regex_search(project, std::regex("\"diskSpeed\"\\s*:\\s*3[^0-9]")));
		CHECK(std::regex_search(project, std::regex("\"board\"\\s*:\\s*\"b\"")));
		CHECK(project.find(sha) != std::string::npos || project.find(lower(sha)) != std::string::npos);
		const size_t d2 = project.find("disk2.img"), d1 = project.find("disk1.cue"), track = project.find("disk1.bin");
		CHECK(d2 != std::string::npos && d1 != std::string::npos && track != std::string::npos && d2 < d1);
		CHECK(project.find("\"support\"") != std::string::npos);
	}
	CHECK(!session.isOpen.get());
	/* board A with its cartridge: made and opened, and it plays; the core
	 * suggests a setting for the game, which is applied, and taken back
	 * when the game is taken out */
	CHECK(wizard.run("chooseMachine", {int64_t{0}}, error).has_value());
	auto fillNow = [&] {
		int64_t v = -1;
		for (int64_t r = 0; r < rowCount("settings"); r++)
			if (text("settings", r, 0) == "initFillByte") fromValue(cell("settings", r, 3), v);
		return v;
	};
	CHECK(wizard.run("removeFile", {std::string("disk"), int64_t{1}}, error).has_value());
	CHECK(wizard.run("removeFile", {std::string("disk"), int64_t{0}}, error).has_value());
	CHECK(wizard.suggestion.get().empty());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && wizard.ready.get());
	CHECK(wizard.suggestion.get() == "The synthetic core suggests a fill of 9.\nApplied below: Initial RAM Fill." && fillNow() == 9);
	/* the game's own setting comes after the package's; one by a package
	 * setting's name is not the game's to declare */
	auto settingRow = [&](const std::string &name) {
		for (int64_t r = 0; r < rowCount("settings"); r++)
			if (text("settings", r, 0) == name) return r;
		return int64_t{-1};
	};
	auto firmwareRow = [&](const std::string &id) {
		for (int64_t r = 0; r < rowCount("firmware"); r++)
			if (text("firmware", r, 0) == id) return r;
		return int64_t{-1};
	};
	CHECK(settingRow("gameLogo") == rowCount("settings") - 1 && text("settings", settingRow("gameLogo"), 1) == "Game Logo");
	CHECK(settingNames().find("initFillByte") == settingNames().rfind("initFillByte") && text("settings", settingRow("initFillByte"), 1) == "Initial RAM Fill");
	CHECK(fillNow() == 9);
	/* its own firmware, needed with its setting on; one by a package
	 * firmware's id is not the game's */
	CHECK(firmwareRow("gamelogo") < 0 && firmwareRow("bios") < 0);
	CHECK(wizard.run("set", {std::string("gameLogo"), std::string("on")}, error).has_value());
	CHECK(firmwareRow("gamelogo") >= 0 && text("firmware", firmwareRow("gamelogo"), 1) == "Game logo" && firmwareRow("bios") < 0);
	CHECK(!wizard.ready.get() && wizard.problems.get().find("Game logo") != std::string::npos);
	/* another game takes them away */
	CHECK(wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value());
	CHECK(wizard.suggestion.get().empty() && fillNow() == 0);
	CHECK(settingRow("gameLogo") < 0 && firmwareRow("gamelogo") < 0);
	/* the same game again: its setting as it was left */
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && fillNow() == 9);
	CHECK(settingRow("gameLogo") >= 0 && firmwareRow("gamelogo") >= 0 && !wizard.ready.get());
	/* with its logo on, the game starts only with the logo given: the open
	 * asks the core again what the game needs */
	std::ofstream(dir / "mylogo.bin") << "a game's logo";
	CHECK(wizard.run("chooseFirmware", {std::string("gamelogo"), (dir / "mylogo.bin").string()}, error).has_value() && wizard.ready.get());
	/* made as Chimera makes one: opened, and written nowhere until its first
	 * save, which says where it goes */
	const fs::path boardA = dir / "boarda.chimeraProject";
	CHECK(wizard.run("create", {}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get(); }, 60));
	CHECK(session.projectPath.get().empty() && session.coreName.get() == "Synth Slots");
	CHECK(wizard.status.get() == "Made a new project: it is written when it is first saved");
	/* a new project starts with the default author, as Chimera's do */
	CHECK(d.until([&] { return details.author.get() == "default user"; }, 20));
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 20));
	CHECK(!session.run("saveProject", {}, error) && error == "this project has not been saved yet: say where it goes");
	CHECK(!session.run("saveAsProject", {boardA.string()}, error));
	CHECK(session.run("saveProject", {boardA.string()}, error).has_value());
	CHECK(d.until([&] { return session.projectPath.get() == boardA.string(); }, 20));
	CHECK(std::regex_search(readFile(boardA), std::regex("\"gameLogo\"\\s*:\\s*\"on\"")));
	/* the renderer, asked apart, is recorded like any setting */
	CHECK(std::regex_search(readFile(boardA), std::regex("\"renderer\"\\s*:\\s*\"software\"")));
	CHECK(session.recentProjects.get().rfind(boardA.string(), 0) == 0);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* opened again on that project's answers, whatever the form held since:
	 * its core, machine, files and settings - its own, so the suggestion is
	 * not put over them - and its firmware found where it was chosen */
	CHECK(wizard.run("chooseCore", {std::string("Synth")}, error).has_value());
	CHECK(wizard.run("openOnLastProject", {}, error).has_value());
	CHECK(wizard.core.get() == "Synth Slots" && wizard.machine.get() == 0 && rowCount("slots") >= 1);
	CHECK(text("slots", 0, 7) == "walker.testrom" && fillNow() == 9);
	CHECK(settingRow("gameLogo") >= 0 && text("settings", settingRow("gameLogo"), 3) == "on");
	CHECK(wizard.suggestion.get() == "The synthetic core suggests a fill of 9.\nThis project's own settings are kept.");
	CHECK(firmwareRow("gamelogo") >= 0 && text("firmware", firmwareRow("gamelogo"), 2) == (dir / "mylogo.bin").string() && wizard.ready.get());
	/* another game: what the core suggested for the project's game comes out
	 * where it still sits - not where the person moved it - and that game's
	 * own setting goes with it (Chimera issue 214) */
	const std::string nofill = (dir / "nofill.testrom").string();
	fs::copy_file(game, nofill);
	std::ofstream(nofill, std::ios::binary | std::ios::app) << "NOFILL";
	auto otherGame = [&] {
		return wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value() &&
			wizard.run("addFile", {std::string("rom"), nofill}, error).has_value();
	};
	CHECK(wizard.run("set", {std::string("initFillByte"), int64_t{5}}, error).has_value());
	CHECK(otherGame() && fillNow() == 5);
	CHECK(wizard.run("openOnLastProject", {}, error).has_value() && fillNow() == 9);
	CHECK(otherGame() && fillNow() == 0);
	CHECK(wizard.suggestion.get() == "The synthetic core suggests nothing for this game.");
	CHECK(settingRow("gameLogo") < 0 && firmwareRow("gamelogo") < 0);
	/* the project's game back is a game like any other now: its suggestion
	 * is applied, and its own setting starts from its default */
	CHECK(wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && fillNow() == 9);
	CHECK(settingRow("gameLogo") >= 0 && text("settings", settingRow("gameLogo"), 3) == "off");
	/* started from a file, as a file dropped on Chimera's window: the first
	 * core that takes its kind, the file in its slot - in place of the one
	 * there when the slot holds no more */
	CHECK(wizard.run("startFrom", {game}, error).has_value() && wizard.core.get() == "Synth" && text("slots", 0, 7) == "walker.testrom");
	CHECK(wizard.run("startFrom", {nofill}, error).has_value() && wizard.core.get() == "Synth" && text("slots", 0, 7) == "nofill.testrom");
	CHECK(!wizard.run("startFrom", {(dir / "notes.txt").string()}, error) && error == "no installed core takes .txt files");
	/* a core the last project was made with opens on its answers first */
	CHECK(wizard.run("startFrom", {(dir / "disk3.dsk").string()}, error).has_value());
	CHECK(wizard.core.get() == "Synth Slots" && wizard.machine.get() == 0 && text("slots", 0, 7) == "walker.testrom" && text("slots", 1, 7) == "disk3.dsk");
	CHECK(wizard.suggestion.get() == "The synthetic core suggests a fill of 9.\nThis project's own settings are kept.");
	/* nothing to make until everything is there */
	CHECK(wizard.run("chooseCore", {std::string("Synth")}, error).has_value());
	CHECK(!wizard.run("create", {(dir / "never.chimeraProject").string()}, error) && error == "not yet: Game: a file is needed");
	fs::remove_all(dir);
	return true;
}

/* A firmware with a hash names one exact file. Another file is refused,
 * except by a game core, which takes a file of the person's own and pins
 * that file's hash in the project */
bool customFirmware(const char *rom, const char *hashedPackage, const char *gamePackage)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(hashedPackage, ec) == 0 || ec || fs::file_size(gamePackage, ec) == 0 || ec)
	{
		std::printf("SKIP custom firmware: the packages with a hashed firmware were not made\n");
		return false;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-logo-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "Cores");
	fs::create_directories(dir / "Firmware");
	fs::copy_file(hashedPackage, dir / "Cores" / "synth-hashed.chimeraCore");
	fs::copy_file(gamePackage, dir / "Cores" / "synth-game.chimeraCore");
	fs::copy_file(gamePackage, dir / "Cores" / "synth-game-again.chimeraCore");
	const std::string game = (dir / "walker.testrom").string();
	fs::copy_file(rom, game);
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", (dir / "Cores").string(), error);
	installed.set("firmwareFolder", (dir / "Firmware").string(), error);
	session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
		[&installed] { return installed.firmwareFolders(); });
	NewProjectViewModel wizard(d, session, installed);
	auto text = [&](const char *rowsName, int64_t row, int32_t column) {
		std::string t;
		for (const auto &r : wizard.rowLists())
			if (r.name == rowsName) fromValue(r.model->cell(row, column), t);
		return t;
	};
	const std::string original = "CE31CFD83A86A850B5B110A3AC05B634F186C1F1";
	const fs::path mine = dir / "mylogo.bin", theirs = dir / "logo.bin";
	std::ofstream(mine, std::ios::binary) << "a logo of my own";
	std::ofstream(theirs, std::ios::binary) << "the original logo";
	char sha[41];
	uint64_t len = 0;
	CHECK(ce_sha1_file(mine.string().c_str(), sha, &len) == 1);
	const std::string mySha = sha;
	CHECK(ce_sha1_file(theirs.string().c_str(), sha, &len) == 1 && lower(sha) == lower(original));
	CHECK(wizard.coreNames.get() == "Synth Game\nSynth Hashed");
	/* game cores and emulators apart; two packages of one core told apart
	 * by the package's hash; the date in Chimera's clock */
	CHECK(std::regex_match(wizard.coreLines.get(), std::regex("Synth Hashed  [0-9a-f]{8}( local)?  -  Synthetic Witness")));
	CHECK(wizard.run("chooseKind", {std::string("games")}, error).has_value());
	CHECK(std::regex_match(wizard.coreLines.get(), std::regex("(Synth Game  2026-10-18 18:35 CEST  \\([0-9a-f]{8}( local)?\\)  \\[package [0-9A-F]{8}\\]  -  Synthetic Witness\n?){2}")));
	CHECK(wizard.run("chooseCoreLine", {int64_t{1}}, error).has_value() && wizard.core.get() == "Synth Game" && wizard.coreLine.get() == 1);
	/* a core chosen by name shows its kind */
	CHECK(wizard.run("chooseCore", {std::string("Synth Hashed")}, error).has_value() && wizard.kind.get() == "emulators" && wizard.coreLine.get() == 0);
	CHECK(wizard.run("chooseCore", {std::string("Synth Game")}, error).has_value() && wizard.kind.get() == "games" && wizard.coreLine.get() >= 0);
	CHECK(wizard.systems.get() == "Synthetic Witness");

	/* not a game: only the file the package names */
	CHECK(wizard.run("chooseCore", {std::string("Synth Hashed")}, error).has_value());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && !wizard.ready.get());
	CHECK(!wizard.run("chooseFirmware", {std::string("logo"), mine.string()}, error) &&
		error == "mylogo.bin is not this file: its hash is " + mySha + ", the requirement is " + original);
	CHECK(!wizard.ready.get());
	CHECK(wizard.run("chooseFirmware", {std::string("logo"), theirs.string()}, error).has_value());
	CHECK(wizard.ready.get() && text("firmware", 0, 3) == "chosen" && wizard.status.get().empty());

	/* a game: a file of the person's own, its hash pinned */
	CHECK(wizard.run("chooseCore", {std::string("Synth Game")}, error).has_value());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && !wizard.ready.get());
	CHECK(wizard.run("chooseFirmware", {std::string("logo"), mine.string()}, error).has_value());
	CHECK(wizard.status.get() == "mylogo.bin is a file of your own, not the original logo: the project pins its hash (" + mySha + ")");
	CHECK(wizard.ready.get() && text("firmware", 0, 2) == mine.string() && text("firmware", 0, 3) == "your own");
	const fs::path made = dir / "mylogo.chimeraProject";
	CHECK(wizard.run("create", {made.string()}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == made.string(); }, 60));
	{
		const std::string project = readFile(made);
		CHECK(project.find(mySha) != std::string::npos || project.find(lower(mySha)) != std::string::npos);
		CHECK(project.find(original) == std::string::npos && project.find(lower(original)) == std::string::npos);
	}
	/* where it was chosen is remembered: that is how the project found it */
	CHECK(installed.chosenFirmware.get().find("\"Synth Game\"") != std::string::npos &&
		installed.chosenFirmware.get().find("mylogo.bin") != std::string::npos);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* a folder scanned finds a file by the hash the package gives, whatever
	 * it is called */
	CHECK(wizard.run("chooseCore", {std::string("Synth Hashed")}, error).has_value());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && !wizard.ready.get());
	fs::create_directories(dir / "scan" / "deeper");
	fs::copy_file(theirs, dir / "scan" / "deeper" / "renamed.bin");
	std::ofstream(dir / "scan" / "logo.bin") << "a logo by the right name and another hash";
	CHECK(wizard.run("scanFolder", {(dir / "scan").string()}, error).has_value());
	CHECK(wizard.status.get() == "Scanned 2 files; 1 requirement resolved" && wizard.ready.get());
	CHECK(text("firmware", 0, 2) == (dir / "scan" / "deeper" / "renamed.bin").string());
	fs::remove_all(dir);
	return true;
}

/* A movie made elsewhere: the core says what it amounts to, the New Project
 * form takes its answer, and the project made has the movie's inputs */
bool movieImport(const char *slotsPackage, const char *rom, const std::string &movies)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(slotsPackage, ec) == 0 || ec || !fs::exists(movies + "/gridWalker.win.txt"))
	{
		std::printf("SKIP movie import: no slots package or no movie\n");
		return false;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "Cores");
	fs::copy_file(slotsPackage, dir / "Cores" / "synth-slots.chimeraCore");
	fs::copy_file(rom, dir / "gridWalker.testrom");
	fs::copy_file(movies + "/gridWalker.win.txt", dir / "win.txt");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", (dir / "Cores").string(), error);
	installed.set("firmwareFolder", (dir / "Firmware").string(), error);
	session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
		[&installed] { return installed.firmwareFolders(); });
	NewProjectViewModel wizard(d, session, installed);
	CHECK(wizard.run("chooseCore", {std::string("Synth Slots")}, error).has_value());
	CHECK(wizard.importMenu.get() == "Import Synth Movie..." && wizard.importMovieFilter.get() == "*.txt" && wizard.importMovieLabel.get() == "A synthetic movie");
	RowModel *files = nullptr;
	for (const auto &r : wizard.rowLists())
		if (r.name == "importFiles") files = r.model;
	CHECK(files != nullptr && files->rowCount() == 1);
	std::string t;
	fromValue(files->cell(0, 0), t);
	CHECK(t == "importRom");
	fromValue(files->cell(0, 2), t);
	CHECK(t == "*.testrom");
	CHECK(!wizard.run("importRun", {}, error) && error == "pick the movie first");
	CHECK(wizard.run("importSetMovie", {(dir / "win.txt").string()}, error).has_value());
	CHECK(!wizard.run("importRun", {}, error) && error == "The game is needed");
	CHECK(wizard.run("importAddFile", {std::string("importRom"), (dir / "gridWalker.testrom").string()}, error).has_value());
	fromValue(files->cell(0, 5), t);
	CHECK(t == "gridWalker.testrom");
	wizard.run("importSetOption", {std::string("importFill"), true}, error);
	/* the form's value before: the movie's answer replaces it */
	CHECK(wizard.run("set", {std::string("initFillByte"), int64_t{5}}, error).has_value());
	CHECK(wizard.run("importRun", {}, error).has_value());
	/* the answer in the form: its file in the slot, its setting, its notes */
	int64_t frames = 0;
	{
		std::istringstream lines(readFile(dir / "win.txt"));
		for (std::string l; std::getline(lines, l);) frames += !l.empty() && l[0] == '|' ? 1 : 0;
	}
	CHECK(wizard.status.get() == "Imported win.txt (" + std::to_string(frames) + " frames)");
	CHECK(wizard.imported.get() && wizard.importNotes.get() == "imported by the synth core" && wizard.title.get() == "win");
	/* the movie's settings are the project's own: the core's note is shown,
	 * and what it would set is not put over them */
	CHECK(wizard.suggestion.get() == "The synthetic core suggests a fill of 9.\nThis project's own settings are kept.");
	RowModel *slots = nullptr, *settings = nullptr;
	for (const auto &r : wizard.rowLists())
	{
		if (r.name == "slots") slots = r.model;
		if (r.name == "settings") settings = r.model;
	}
	fromValue(slots->cell(0, 7), t);
	CHECK(t == "gridWalker.testrom" && wizard.ready.get());
	int64_t fill = -1;
	for (int64_t r = 0; r < settings->rowCount(); r++)
	{
		std::string n;
		fromValue(settings->cell(r, 0), n);
		if (n == "initFillByte") fromValue(settings->cell(r, 3), fill);
	}
	/* what the synthetic importer dictates for a ticked option (sent as true,
	 * as Chimera sends it, which it reads as no number) */
	CHECK(fill == 0);
	/* made: the project has the movie's inputs, and opens on them */
	const fs::path project = dir / "win.chimeraProject";
	CHECK(wizard.run("create", {project.string()}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == project.string(); }, 60));
	CHECK(inputs.size() == frames);
	/* forgotten: the next project starts empty */
	wizard.run("importForget", {}, error);
	CHECK(!wizard.imported.get());
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir);
	return true;
}

/* Open Game asks only for the game: the installed core that takes its kind of
 * file opens it, and when two do, both are offered */
bool openByKind(const char *package, const char *rom, const char *again)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(again, ec) == 0 || ec)
	{
		std::printf("SKIP choosing between cores: no second package was made\n");
		return false;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-kind-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const fs::path cores = dir / "Cores";
	fs::create_directories(cores);
	fs::copy_file(package, cores / "synth-box.chimeraCore");
	/* the case of the name does not matter */
	const std::string game = (dir / "Walker.TestRom").string();
	fs::copy_file(rom, game);
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", cores.string(), error);
	const auto takes = installed.coresFor("testrom");
	CHECK(takes.size() == 1 && takes[0].first == "Synth" && installed.coresFor("md").empty());
	CHECK(installed.rowLists()[0].model->columnName(3) == "takes");
	std::string column;
	fromValue(installed.rowLists()[0].model->cell(0, 3), column);
	CHECK(column == ".testrom");
	session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
		[&installed] { return installed.firmwareFolders(); },
		[&installed](const std::string &ext) { return installed.coresFor(ext); });

	/* a kind of file no core takes says so */
	error.clear();
	CHECK(!session.run("openGame", {(dir / "notes.txt").string()}, error));
	CHECK(error == "no installed core takes .txt files");

	/* one core takes it: it opens */
	int64_t was = session.opens.get();
	CHECK(session.run("openGame", {game}, error).has_value());
	CHECK(d.until([&] { return session.opens.get() > was; }, 60));
	CHECK(session.isOpen.get() && session.coreName.get() == "Synth" && session.chooseFor.get().empty());
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));

	/* two do: both are offered and nothing opens until one is chosen */
	fs::copy_file(again, cores / "synth-again.chimeraCore");
	installed.run("refresh", {}, error);
	CHECK(installed.coresFor("testrom").size() == 2);
	was = session.opens.get();
	session.run("openGame", {game}, error);
	CHECK(session.chooseFor.get() == game && session.choices.get() == "Synth\nSynth Again");
	d.until([] { return false; }, 0.2);
	CHECK(session.opens.get() == was && !session.isOpen.get());
	CHECK(!session.run("chooseCore", {int64_t{2}}, error));
	CHECK(session.run("chooseCore", {int64_t{1}}, error).has_value());
	CHECK(d.until([&] { return session.opens.get() > was; }, 60));
	CHECK(session.coreName.get() == "Synth Again" && session.chooseFor.get().empty() && session.choices.get().empty());

	/* and turning the offer down opens nothing */
	was = session.opens.get();
	session.run("openGame", {game}, error);
	CHECK(session.chooseFor.get() == game);
	CHECK(session.run("chooseCore", {int64_t{-1}}, error).has_value());
	d.until([] { return false; }, 0.2);
	CHECK(session.chooseFor.get().empty() && session.opens.get() == was && session.coreName.get() == "Synth Again");
	CHECK(!session.run("chooseCore", {int64_t{0}}, error));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir);
	return true;
}

} // namespace

/* Chimera's clock for a core's date (chimera issue 331): the cases of its
 * own test, EuropeanClockTests */
void europeanClock()
{
	auto at = [](const char *iso) {
		int64_t t = 0;
		return parseIsoTime(iso, t) ? europeanTime(t) : std::string("unread");
	};
	CHECK(at("2026-10-18T16:35:00Z") == "2026-10-18 18:35 CEST");
	CHECK(at("2026-01-15T12:00:00Z") == "2026-01-15 13:00 CET");
	CHECK(at("2026-03-29T00:59:00Z") == "2026-03-29 01:59 CET" && at("2026-03-29T01:00:00Z") == "2026-03-29 03:00 CEST");
	CHECK(at("2026-10-25T00:59:00Z") == "2026-10-25 02:59 CEST" && at("2026-10-25T01:00:00Z") == "2026-10-25 02:00 CET");
	CHECK(at("2026-11-30T23:30:00Z") == "2026-12-01 00:30 CET");
	CHECK(at("2026-10-18T12:35:00-04:00") == "2026-10-18 18:35 CEST" && at("2026-10-18 16:35") == "2026-10-18 18:35 CEST");
	CHECK(at("2026-10-18T16:35:00.123Z") == "2026-10-18 18:35 CEST" && at("yesterday") == "unread" && at("2026-13-01T00:00Z") == "unread");
}

int main(int argc, char **argv)
{
	signalsAndProperties();
	registry();
	bindings();
	lua();
	luaConsole();
	configStore();
	firmware();
	settingsByMachine();
	pianoRollWithoutAMachine();
	watchValues();
	themes();
	europeanClock();
	const bool ranPads = gamepads();
	if (ranPads) padCatch();
	bool ranMachine = false;
	if (argc >= 3)
	{
		FILE *p = std::fopen(argv[1], "rb"), *r = std::fopen(argv[2], "rb");
		if (p != nullptr && r != nullptr)
		{
			ranMachine = machine(argv[1], argv[2]);
			ranMachine = ramSearch(argv[1], argv[2]) && ranMachine;
			ranMachine = settings(argv[1], argv[2]) && ranMachine;
			ranMachine = newProject(argv[1], argv[2]) && ranMachine;
			ranMachine = rerecordCount(argv[1], argv[2]) && ranMachine;
			ranMachine = ramWatch(argv[1], argv[2]) && ranMachine;
			ranMachine = hexEditor(argv[1], argv[2]) && ranMachine;
			ranMachine = pianoRollEditing(argv[1], argv[2]) && ranMachine;
			ranMachine = controlsAndHotkeys(argv[1], argv[2]) && ranMachine;
			ranMachine = greenzoneChoice(argv[1], argv[2]) && ranMachine;
			ranMachine = speedAndSound(argv[1], argv[2]) && ranMachine;
			ranMachine = overlay(argv[1], argv[2]) && ranMachine;
			ranMachine = projectDetails(argv[1], argv[2]) && ranMachine;
			ranMachine = pianoRollColumns(argv[1], argv[2]) && ranMachine;
			ranMachine = backupAndHistory(argv[1], argv[2]) && ranMachine;
			ranMachine = autoholdAndAutofire(argv[1], argv[2]) && ranMachine;
			ranMachine = tastudioSettings(argv[1], argv[2]) && ranMachine;
			if (argc >= 5) ranMachine = project(argv[1], argv[2], argv[3], argv[4]) && ranMachine;
			else std::printf("SKIP the project tests: no movies and goldens folders given\n");
			if (argc >= 6) ranMachine = openByKind(argv[1], argv[2], argv[5]) && ranMachine;
			else std::printf("SKIP choosing between cores: no second package given\n");
			if (argc >= 7) ranMachine = newProjectWizard(argv[1], argv[2], argv[6]) && ranMachine;
			if (argc >= 7) ranMachine = pianoRollAxes(argv[6], argv[2]) && ranMachine;
			if (argc >= 7) ranMachine = movieImport(argv[6], argv[2], argv[3]) && ranMachine;
			else std::printf("SKIP New Project: no slots package given\n");
			if (argc >= 9) ranMachine = customFirmware(argv[2], argv[7], argv[8]) && ranMachine;
			else std::printf("SKIP custom firmware: no packages with a hashed firmware given\n");
		}
		else
			std::printf("SKIP the machine tests: %s or %s is missing\n", argv[1], argv[2]);
		if (p) std::fclose(p);
		if (r) std::fclose(r);
	}
	else
		std::printf("SKIP the machine tests: no package and rom given\n");
	std::printf("%d checks, %d failed%s%s\n", checks, failures, ranMachine ? "" : " (machine tests skipped)",
		ranPads ? "" : " (gamepad tests skipped)");
	return failures == 0 ? 0 : 1;
}
