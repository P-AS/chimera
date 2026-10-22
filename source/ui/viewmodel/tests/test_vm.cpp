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
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#ifndef _WIN32
#include <dlfcn.h>
#endif

#include "chimera/vm/bindings.hpp"
#include "chimera/vm/gamepads.hpp"
#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/piano_roll_vm.hpp"
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
	auto n = roll.run("fill", {int64_t{1000000}}, error);
	CHECK(n && std::get<int64_t>(*n) == 1000000);
	CHECK(roll.frameCount.get() == 1000000);
	CHECK(roll.rowLists().size() == 1 && roll.rowLists()[0].model->rowCount() == 1000000);
	/* no machine, so no buttons to flip */
	CHECK(!roll.run("toggle", {int64_t{5}, int64_t{0}}, error) && error == "no such frame or button");
	/* the view says how many rows fit; the first row in view stays inside the log */
	roll.visibleFrames.set(40);
	CHECK(roll.set("topFrame", int64_t{5000000}, error) && roll.topFrame.get() == 999999);
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

	/* a press on the cell flips it; a drag down the column gives the next cells the same */
	auto held = roll.run("press", {int64_t{0}, int64_t{4}}, error);
	CHECK(held && std::get<bool>(*held) == false && inputs.at(0) == 0);
	roll.run("fill", {int64_t{10}}, error);
	roll.run("press", {int64_t{2}, int64_t{7}}, error);
	roll.run("drag", {int64_t{3}, int64_t{7}}, error);
	roll.run("drag", {int64_t{4}, int64_t{7}}, error);
	CHECK(inputs.at(2) == (uint64_t{1} << 7) && inputs.at(3) == (uint64_t{1} << 7) && inputs.at(4) == (uint64_t{1} << 7));
	CHECK(changedRows >= 4);

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

	/* an edit survives the trip: P1 A on frame 10 */
	const std::string unedited = readFile(dir / "saved.chimeraProject");
	roll.run("toggle", {int64_t{10}, int64_t{4}}, error);
	session.run("saveProject", {std::string()}, error);
	CHECK(d.until([&] { return readFile(dir / "saved.chimeraProject") != unedited; }, 20));
	CHECK(openIt(dir / "p.chimeraProject"));
	CHECK(inputs.at(10) == before[10]);
	CHECK(openIt(dir / "saved.chimeraProject"));
	CHECK(inputs.at(10) == (before[10] | (uint64_t{1} << 4)));
	/* and a project whose file is missing says which */
	fs::remove(dir / "gridWalker.testrom");
	session.run("openProject", {(dir / "p.chimeraProject").string(), std::string(package)}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	CHECK(session.status.get().find("gridWalker.testrom (not found)") != std::string::npos);
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
	pianoRollWithoutAMachine();
	const bool ranPads = gamepads();
	bool ranMachine = false;
	if (argc >= 3)
	{
		FILE *p = std::fopen(argv[1], "rb"), *r = std::fopen(argv[2], "rb");
		if (p != nullptr && r != nullptr) ranMachine = machine(argv[1], argv[2]);
		if (p != nullptr && r != nullptr && argc >= 5) ranMachine = project(argv[1], argv[2], argv[3], argv[4]) && ranMachine;
		else std::printf("SKIP the machine tests: %s or %s is missing\n", argv[1], argv[2]);
		if (p) std::fclose(p);
		if (r) std::fclose(r);
	}
	else
		std::printf("SKIP the machine tests: no package and rom given\n");
	std::printf("%d checks, %d failed%s%s\n", checks, failures, ranMachine ? "" : " (machine tests skipped)",
		ranPads ? "" : " (gamepad tests skipped)");
	return failures == 0 ? 0 : 1;
}
