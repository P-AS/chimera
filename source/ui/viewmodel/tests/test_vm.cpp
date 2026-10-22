/* The view models with no window: what a script, the MCP server or a test
 * sees. No Qt is linked here; the dispatcher is drained by hand.
 *
 *   test_vm [<synth-box package> <synth test rom>]
 *
 * Without the package and rom the machine tests are skipped (exit 77 when
 * nothing else ran). */

#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "chimera/vm/bindings.hpp"
#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/session_vm.hpp"

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
	session.run("pause", {}, error);
	CHECK(d.until([&] { return !session.running.get(); }, 5));
	const int64_t stopped = session.frame.get();
	d.until([] { return false; }, 0.2);
	CHECK(session.frame.get() == stopped);
	CHECK(inputs.at(3) == (uint64_t{1} << 7)); /* played, not overwritten */
	CHECK(inputs.size() >= stopped);

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

} // namespace

int main(int argc, char **argv)
{
	signalsAndProperties();
	registry();
	bindings();
	pianoRollWithoutAMachine();
	bool ranMachine = false;
	if (argc >= 3)
	{
		FILE *p = std::fopen(argv[1], "rb"), *r = std::fopen(argv[2], "rb");
		if (p != nullptr && r != nullptr) ranMachine = machine(argv[1], argv[2]);
		else std::printf("SKIP the machine tests: %s or %s is missing\n", argv[1], argv[2]);
		if (p) std::fclose(p);
		if (r) std::fclose(r);
	}
	else
		std::printf("SKIP the machine tests: no package and rom given\n");
	std::printf("%d checks, %d failed%s\n", checks, failures, ranMachine ? "" : " (machine tests skipped)");
	return failures == 0 ? 0 : 1;
}
