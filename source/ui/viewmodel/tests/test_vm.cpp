/* The view models with no window: what a script, the MCP server or a test
 * sees. No Qt is linked here; the dispatcher is drained by hand.
 *
 *   test_vm [<synth-box package> <synth test rom> [<synth movies dir> <synth goldens dir>]]
 *
 * Without the package and rom the machine tests are skipped (exit 77 when
 * nothing else ran). */

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#ifndef _WIN32
#include <dlfcn.h>
#endif

#include "chimera/vm/bindings.hpp"
#include "chimera/vm/config_store.hpp"
#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/firmware.hpp"
#include "chimera/vm/branches_vm.hpp"
#include "chimera/vm/gamepads.hpp"
#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/lua_console_vm.hpp"
#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/ram_search_vm.hpp"
#include "chimera/vm/settings_vm.hpp"
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
	for (size_t a = 0; a < before.size() && a < after.size(); a++)
		if (before[a] != after[a])
		{
			char buf[24];
			std::snprintf(buf, sizeof buf, "%06zX", a);
			changed.emplace_back(buf);
		}
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
	/* the value column is the byte now */
	if (!listed.empty())
	{
		int64_t v = -1;
		fromValue(rows.cell(0, 1), v);
		CHECK(v == after[std::stoul(listed[0], nullptr, 16)]);
	}
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
	 * branch loaded - and the machine is where the winning movie leaves it */
	CHECK(openIt(dir / "p.chimeraProject"));
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	CHECK(branches.run("create", {std::string("win")}, error).has_value() && branches.count.get() == 1);
	roll.run("addMarker", {int64_t{20}, std::string("in the branch")}, error);
	branches.run("remove", {int64_t{0}}, error);
	CHECK(branches.count.get() == 0);
	branches.run("create", {std::string("win")}, error);
	roll.run("removeMarker", {int64_t{20}}, error);
	roll.run("paint", {int64_t{0}, end - 1, int64_t{1}, false}, error);
	CHECK(d.until([&] { return session.frame.get() == end && session.greenEnd.get() == end; }, 30));
	CHECK(session.readDomain("RAM") != std::vector<uint8_t>(ram.begin(), ram.end()));
	CHECK(branches.run("load", {int64_t{0}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == end && session.greenEnd.get() == end; }, 30));
	d.until([] { return false; }, 0.1);
	CHECK(session.readDomain("RAM") == std::vector<uint8_t>(ram.begin(), ram.end()));
	CHECK(session.readDomain("VRAM") == std::vector<uint8_t>(vram.begin(), vram.end()));
	CHECK(roll.frameCount.get() == end && roll.markerCount.get() == 1 && roll.rowLists()[0].model->rowNote(20) == "in the branch");
	CHECK(!branches.run("load", {int64_t{3}}, error) && error == "no branch 3");
	/* kept with the project */
	session.run("saveProject", {(dir / "branched.chimeraProject").string()}, error);
	CHECK(d.until([&] { return session.projectPath.get() == (dir / "branched.chimeraProject").string(); }, 20));
	CHECK(openIt(dir / "p.chimeraProject"));
	CHECK(branches.count.get() == 0);
	CHECK(openIt(dir / "branched.chimeraProject"));
	CHECK(branches.count.get() == 1);
	std::string branchName;
	int64_t branchFrames = 0;
	fromValue(branches.rowLists()[0].model->cell(0, 0), branchName);
	fromValue(branches.rowLists()[0].model->cell(0, 3), branchFrames);
	CHECK(branchName == "win" && branchFrames == end);
	roll.run("paint", {int64_t{0}, end - 1, int64_t{1}, false}, error);
	branches.run("load", {int64_t{0}}, error);
	CHECK(d.until([&] { return session.frame.get() == end && session.greenEnd.get() == end; }, 30));
	d.until([] { return false; }, 0.1);
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
	const bool ranPads = gamepads();
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
			if (argc >= 5) ranMachine = project(argv[1], argv[2], argv[3], argv[4]) && ranMachine;
			else std::printf("SKIP the project tests: no movies and goldens folders given\n");
			if (argc >= 6) ranMachine = openByKind(argv[1], argv[2], argv[5]) && ranMachine;
			else std::printf("SKIP choosing between cores: no second package given\n");
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
