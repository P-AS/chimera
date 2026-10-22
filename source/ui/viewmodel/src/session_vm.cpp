#include "chimera/vm/session_vm.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "chimera/engine.h"

namespace chimera::vm {

SessionViewModel::SessionViewModel(Dispatcher &dispatcher, InputLog &inputs)
	: ViewModel("session", dispatcher), inputs_(inputs)
{
	expose("isOpen", "whether a machine is open", isOpen);
	expose("running", "whether the machine is running on its own", running);
	expose("frame", "the frame the machine is about to run", frame);
	expose("fps", "frames run in the last half second, per second", fps);
	expose("status", "what the session last had to say", status);
	expose("coreName", "the open core", coreName);
	expose("systemName", "the machine the core runs", systemName);
	expose("buttonCount", "how many buttons the machine has", buttonCount);
	expose("gamepads", "the gamepads in use, or why there are none", gamepads);
	expose("sound", "\"Sound on\", or why there is none", sound);
	expose("soundQueued", "sample pairs sent to the speakers", soundQueued);
	expose("soundDropped", "sample pairs dropped because the machine ran ahead of the speakers", soundDropped);
	picture("screen", "the machine's picture", screen, screenChanged);

	command("open", "opens a core package with a game file", {"package", "game"},
		[this](const Args &a, std::string &error) -> Value {
			std::string package, game;
			if (!arg(a, 0, package, error) || !arg(a, 1, game, error)) return {};
			status.set("Opening " + game + "...");
			submit([this, package, game] { openOnMachine(package, game); });
			return {};
		});
	command("close", "closes the machine", {}, [this](const Args &, std::string &) -> Value {
		submit([this] { closeOnMachine(); });
		return {};
	}, &isOpen);
	command("play", "runs the machine at its own speed", {}, [this](const Args &, std::string &) -> Value {
		submit([this] {
			if (s_ == nullptr) return;
			runningM_ = true;
			due_ = fpsSince_ = Clock::now();
			fpsFrames_ = 0;
			later([this] { running.set(true); });
		});
		return {};
	}, &isOpen);
	command("pause", "stops the machine after the frame it is on", {}, [this](const Args &, std::string &) -> Value {
		submit([this] {
			runningM_ = false;
			later([this] { running.set(false); fps.set(0.0); });
		});
		return {};
	}, &isOpen);
	command("frameAdvance", "runs one frame and stops", {}, [this](const Args &, std::string &) -> Value {
		submit([this] {
			if (s_ == nullptr) return;
			runningM_ = false;
			advanceOnMachine();
			later([this] { running.set(false); });
		});
		return {};
	}, &isOpen);
	command("restart", "opens the same core and game again at frame 0; the inputs stay", {},
		[this](const Args &, std::string &) -> Value {
			submit([this] {
				const std::string package = package_, game = game_;
				if (!package.empty()) openOnMachine(package, game);
			});
			return {};
		}, &isOpen);
	command("key", "a key went down (true) or up (false), by Chimera's key name", {"name", "down"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			bool down = false;
			if (!arg(a, 0, name, error) || !arg(a, 1, down, error)) return {};
			key(name, down);
			return {};
		});

	thread_ = std::thread([this] { loop(); });
}

SessionViewModel::~SessionViewModel()
{
	{
		std::lock_guard<std::mutex> lock(m_);
		quit_ = true;
	}
	cv_.notify_all();
	thread_.join();
}

void SessionViewModel::key(const std::string &name, bool down)
{
	bool &was = keysDown_[name];
	if (was == down) return;
	was = down;
	uint64_t mask = 0;
	for (const auto &[k, isDown] : keysDown_)
		if (isDown)
			for (int b : bindings_.buttonsOf(k))
				if (b < 64) mask |= uint64_t{1} << b;
	held_.store(mask);
}

void SessionViewModel::submit(std::function<void()> job)
{
	{
		std::lock_guard<std::mutex> lock(m_);
		jobs_.push_back(std::move(job));
	}
	cv_.notify_all();
}

void SessionViewModel::loop()
{
	for (;;)
	{
		std::function<void()> job;
		bool advance = false;
		{
			std::unique_lock<std::mutex> lock(m_);
			for (;;)
			{
				if (quit_) break;
				if (!jobs_.empty())
				{
					job = std::move(jobs_.front());
					jobs_.pop_front();
					break;
				}
				if (runningM_ && s_ != nullptr)
				{
					if (Clock::now() >= due_) { advance = true; break; }
					cv_.wait_until(lock, due_);
				}
				else
					cv_.wait(lock);
			}
			if (quit_) break;
		}
		if (job)
		{
			job();
			continue;
		}
		if (advance)
		{
			advanceOnMachine();
			due_ += period_;
			/* behind by more than a few frames (a slow frame, a busy machine):
			 * carry on from now rather than race to catch up */
			const auto now = Clock::now();
			if (now - due_ > 4 * period_) due_ = now;
		}
	}
	if (s_ != nullptr)
	{
		ce_session_free(s_);
		s_ = nullptr;
	}
	padsM_.reset(); /* SDL stops on the thread that started it */
	soundM_.reset();
}

void SessionViewModel::openOnMachine(const std::string &package, const std::string &game)
{
	if (s_ != nullptr)
	{
		ce_session_free(s_);
		s_ = nullptr;
	}
	runningM_ = false;

	std::ifstream in(game, std::ios::binary);
	if (!in)
	{
		later([this, game] { status.set("Could not read " + game); isOpen.set(false); });
		return;
	}
	const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const char *error = nullptr;
	ce_session *s = ce_session_open(package.c_str(), bytes.data(), bytes.size(), game.c_str(), nullptr,
		nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0, &error);
	if (s == nullptr)
	{
		const std::string why = error != nullptr ? error : "no reason given";
		later([this, why] { status.set("Could not open: " + why); isOpen.set(false); });
		return;
	}
	s_ = s;
	frameM_ = 0;
	package_ = package;
	game_ = game;

	std::vector<std::string> names;
	const int64_t count = ce_session_button_count(s);
	for (int64_t i = 0; i < count; i++)
	{
		const char *n = ce_session_button_name(s, i);
		names.emplace_back(n != nullptr ? n : "");
	}
	const char *mn = ce_session_button_mnemonics(s);
	const std::string mnemonics = mn != nullptr ? mn : "";

	std::string keybinds;
	const char *perr = nullptr;
	if (ce_package *p = ce_package_open(package.c_str(), &perr))
	{
		uint64_t len = 0;
		if (const uint8_t *d = ce_package_entry(p, "default_keybinds.json", &len))
			keybinds.assign(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
		ce_package_free(p);
	}

	std::string bindError;
	bindingsM_ = Bindings();
	if (!keybinds.empty()) bindingsM_.parse(keybinds, names, bindError);
	if (!padsM_) padsM_ = std::make_unique<Gamepads>();
	padCount_ = 0;
	std::string padText = padsM_->available() ? "No gamepads" : padsM_->problem();
	if (!soundM_) soundM_ = std::make_unique<Sound>();
	const std::string soundText = soundM_->available() ? "Sound on" : soundM_->problem();
	later([this, padText, soundText] { gamepads.set(padText); sound.set(soundText); });

	const int32_t num = ce_session_vsync_numerator(s), den = ce_session_vsync_denominator(s);
	if (num > 0 && den > 0)
		period_ = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(static_cast<double>(den) / num));
	const char *core = ce_session_core_name(s);
	const char *system = ce_session_system_name(s);
	const std::string coreText = core != nullptr ? core : "", systemText = system != nullptr ? system : "";
	const double rate = num > 0 && den > 0 ? static_cast<double>(num) / den : 0;

	later([this, names, mnemonics, keybinds, coreText, systemText, rate, game] {
		buttonNames_ = names;
		mnemonics_ = mnemonics;
		std::string why;
		const bool bound = keybinds.empty() || bindings_.parse(keybinds, buttonNames_, why);
		buttonCount.set(static_cast<int64_t>(names.size()));
		coreName.set(coreText);
		systemName.set(systemText);
		frame.set(0);
		running.set(false);
		isOpen.set(true);
		char hz[32];
		std::snprintf(hz, sizeof hz, "%.3f", rate);
		status.set("Opened " + game + " (" + hz + " frames a second, " + std::to_string(bindings_.boundKeys()) +
			" keys bound" + (bound ? "" : "; the package's bindings are not readable: " + why) + ")");
		buttonsChanged.emit();
	});
}

void SessionViewModel::closeOnMachine()
{
	if (s_ == nullptr) return;
	ce_session_free(s_);
	s_ = nullptr;
	runningM_ = false;
	later([this] {
		isOpen.set(false);
		running.set(false);
		fps.set(0.0);
		status.set("Closed");
	});
}

void SessionViewModel::advanceOnMachine()
{
	const int64_t f = frameM_;
	uint64_t padMask = 0;
	if (padsM_)
	{
		padsM_->poll(padHeld_);
		for (const std::string &name : padHeld_)
			for (int b : bindingsM_.buttonsOf(name))
				if (b < 64) padMask |= uint64_t{1} << b;
		const std::vector<std::string> open = padsM_->names();
		if (open.size() != padCount_)
		{
			padCount_ = open.size();
			std::string text = open.empty() ? "No gamepads" : std::to_string(open.size()) + (open.size() == 1 ? " gamepad:" : " gamepads:");
			for (const std::string &n : open) text += " " + n;
			later([this, text] { gamepads.set(text); });
		}
	}
	const uint64_t held = held_.load() | padMask;
	const int64_t size = inputs_.size();
	const uint64_t logged = inputs_.at(f);
	const uint64_t mask = logged | held;
	bool changed = false, appended = false;
	if (f >= size)
	{
		inputs_.set(f, mask);
		appended = true;
	}
	else if (mask != logged)
	{
		inputs_.set(f, mask);
		changed = true;
	}

	ce_session_frame_advance(s_, mask, 1);
	frameM_++;
	const char *death = ce_session_guest_death(s_);
	if (death != nullptr && *death != '\0')
	{
		runningM_ = false;
		const std::string why = death;
		later([this, why] { running.set(false); status.set("The machine stopped: " + why); });
	}

	if (soundM_)
	{
		int32_t pairs = 0;
		const int16_t *samples = ce_session_audio(s_, &pairs);
		soundM_->play(samples, pairs);
	}

	const uint32_t *px = ce_session_video(s_);
	const int32_t w = ce_session_video_width(s_), h = ce_session_video_height(s_);
	if (px != nullptr && w > 0 && h > 0)
	{
		/* the machine's own answer, else its declared virtual size */
		int32_t ax = 0, ay = 0;
		if (ce_session_display_aspect(s_, &ax, &ay) == 0 || ax <= 0 || ay <= 0)
			ax = ce_session_virtual_width(s_), ay = ce_session_virtual_height(s_);
		const double aspect = ax > 0 && ay > 0 ? static_cast<double>(ax) / ay : static_cast<double>(w) / h;
		screen.publish(px, w, h, aspect);
	}

	double measured = -1;
	fpsFrames_++;
	const auto now = Clock::now();
	const double secs = std::chrono::duration<double>(now - fpsSince_).count();
	if (secs >= 0.5)
	{
		measured = fpsFrames_ / secs;
		fpsFrames_ = 0;
		fpsSince_ = now;
	}

	bool post = false;
	{
		std::lock_guard<std::mutex> lock(pendingM_);
		pendingFrame_ = frameM_;
		if (measured >= 0) pendingFps_ = measured;
		if (soundM_)
		{
			pendingQueued_ = soundM_->queued();
			pendingDropped_ = soundM_->dropped();
		}
		if (changed)
		{
			changedFirst_ = changedFirst_ < 0 ? f : std::min(changedFirst_, f);
			changedLast_ = std::max(changedLast_, f);
		}
		if (appended)
		{
			appendedFirst_ = appendedFirst_ < 0 ? size : std::min(appendedFirst_, size);
			appendedLast_ = std::max(appendedLast_, f);
		}
		if (!tickPosted_) post = tickPosted_ = true;
	}
	if (post) later([this] { tick(); });
}

void SessionViewModel::tick()
{
	int64_t f, cf, cl, af, al;
	double measured;
	int64_t queued, dropped;
	{
		std::lock_guard<std::mutex> lock(pendingM_);
		tickPosted_ = false;
		f = pendingFrame_;
		measured = pendingFps_;
		cf = changedFirst_, cl = changedLast_, af = appendedFirst_, al = appendedLast_;
		queued = pendingQueued_, dropped = pendingDropped_;
		pendingFps_ = -1;
		changedFirst_ = changedLast_ = appendedFirst_ = appendedLast_ = -1;
	}
	frame.set(f);
	if (measured >= 0) fps.set(measured);
	soundQueued.set(queued);
	soundDropped.set(dropped);
	if (cf >= 0) inputsChanged.emit(cf, cl);
	if (af >= 0) inputsAppended.emit(af, al);
	screenChanged.emit();
}

} // namespace chimera::vm
