#include "chimera/vm/session_vm.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <future>
#include <iterator>

#include "chimera/engine.h"

namespace chimera::vm {

SessionViewModel::SessionViewModel(Dispatcher &dispatcher, InputLog &inputs)
	: ViewModel("session", dispatcher), inputs_(inputs)
{
	expose("isOpen", "whether a machine is open", isOpen);
	expose("opens", "how many times a machine has been opened", opens);
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
	expose("greenEnd", "frames before this one were run with the inputs they have now", greenEnd);
	expose("greenzoneStates", "machine states the greenzone holds", greenzoneStates);
	expose("autoRestore", "after an edit before the current frame, run on to where the machine was", autoRestore, true);
	expose("lastSeekMs", "how long the last seek took, in milliseconds", lastSeekMs);
	expose("lastSeekFrames", "how many frames the last seek ran", lastSeekFrames);
	expose("projectPath", "the open project's file, \"\" when a game was opened without one", projectPath);
	expose("projectTitle", "the open project's title", projectTitle);
	picture("screen", "the machine's picture", screen, screenChanged);
	autoRestoreWatch_ = autoRestore.changed.connect([this] { autoRestoreM_.store(autoRestore.get()); });

	command("open", "opens a core package with a game file", {"package", "game"},
		[this](const Args &a, std::string &error) -> Value {
			std::string package, game;
			if (!arg(a, 0, package, error) || !arg(a, 1, game, error)) return {};
			status.set("Opening " + game + "...");
			/* another game starts from its package's own settings */
			submit([this, package, game] {
				settingsM_.clear();
				markersM_.clear();
				later([this] { markersLoaded.emit({}); });
				openOnMachine(package, game);
			});
			return {};
		});
	command("openProject", "opens a Chimera project with a core package; its files are looked for beside it",
		{"project", "package"}, [this](const Args &a, std::string &error) -> Value {
			std::string project, package;
			if (!arg(a, 0, project, error) || !arg(a, 1, package, error)) return {};
			status.set("Opening " + project + "...");
			submit([this, project, package] { openProjectOnMachine(project, package, false); });
			return {};
		});
	command("saveProject", "saves the open project with its inputs, to its own file or to the one given",
		{"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!a.empty() && !arg(a, 0, path, error)) return {};
			if (projectPath.get().empty())
			{
				error = "no project is open";
				return {};
			}
			submit([this, path] { saveProjectOnMachine(path); });
			return {};
		}, &isOpen);
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
	command("seek", "puts the machine on a frame, from the nearest stored state; stops it running", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t target = 0;
			if (!arg(a, 0, target, error)) return {};
			if (target < 0)
			{
				error = "no frame before 0";
				return {};
			}
			submit([this, target] {
				runningM_ = false;
				later([this] { running.set(false); fps.set(0.0); });
				seekOnMachine(target);
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
	freeSessionOnMachine();
	if (projectM_ != nullptr) ce_project_free(projectM_);
	projectM_ = nullptr;
	padsM_.reset(); /* SDL stops on the thread that started it */
	soundM_.reset();
}

void SessionViewModel::openOnMachine(const std::string &package, const std::string &game)
{
	freeSessionOnMachine();
	runningM_ = false;

	std::ifstream in(game, std::ios::binary);
	if (!in)
	{
		later([this, game] { status.set("Could not read " + game); isOpen.set(false); });
		return;
	}
	const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const char *error = nullptr;
	ce_session *s = ce_session_open(package.c_str(), bytes.data(), bytes.size(), game.c_str(),
		settingsM_.empty() ? nullptr : settingsM_.c_str(),
		nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0, &error);
	if (s == nullptr)
	{
		const std::string why = error != nullptr ? error : "no reason given";
		later([this, why] { status.set("Could not open: " + why); isOpen.set(false); });
		return;
	}
	freeProjectOnMachine();
	later([this] { projectPath.set(""); projectTitle.set(""); });
	adoptOnMachine(s, package, game, false);
	package_ = package;
	game_ = game;
}

void SessionViewModel::freeSessionOnMachine()
{
	if (s_ == nullptr) return;
	for (const Hooks &h : hooksM_)
		if (h.closing) h.closing(s_);
	ce_session_free(s_);
	s_ = nullptr;
}

void SessionViewModel::onMachine(MachineJob job)
{
	submit([this, job = std::move(job)] { job(s_); });
}

std::string SessionViewModel::settingsOnMachine() const
{
	if (projectM_ != nullptr)
	{
		uint64_t len = 0;
		const char *t = ce_project_settings_text(projectM_, &len);
		return t != nullptr ? std::string(t, static_cast<size_t>(len)) : std::string("{}");
	}
	return settingsM_.empty() ? std::string("{}") : settingsM_;
}

std::string SessionViewModel::slotsOnMachine() const
{
	if (projectM_ == nullptr) return "{}";
	uint64_t len = 0;
	const char *t = ce_project_slots_text(projectM_, &len);
	return t != nullptr ? std::string(t, static_cast<size_t>(len)) : std::string("{}");
}

void SessionViewModel::changeSettings(std::string json, std::function<void(bool, std::string)> done)
{
	submit([this, json, done] {
		auto answer = [this, done](bool ok, std::string what) { later([done, ok, what] { done(ok, what); }); };
		if (s_ == nullptr)
		{
			answer(false, "No machine is open");
			return;
		}
		const int32_t live = ce_session_apply_settings(s_, json.c_str());
		if (live == 2)
		{
			answer(false, std::string("The core refused them: ") + ce_session_last_error(s_));
			return;
		}
		/* kept for the next open either way: the project's, or the game's */
		if (projectM_ != nullptr)
		{
			const char *error = nullptr;
			if (ce_project_set_settings_text(projectM_, json.c_str(), &error) != 0)
			{
				answer(false, std::string("The project refused them: ") + (error != nullptr ? error : "?"));
				return;
			}
		}
		else
			settingsM_ = json;
		if (live == 0)
		{
			answer(true, "Applied to the running machine");
			return;
		}
		/* the core takes them only at start: open it again with them */
		runningM_ = false;
		later([this] { running.set(false); });
		if (projectM_ != nullptr) openProjectOnMachine(projectPathM_, package_, true, projectM_);
		else openOnMachine(package_, game_);
		answer(s_ != nullptr, s_ != nullptr ? "The machine started again with them, at frame 0"
											: "The machine would not start again with them");
	});
}

void SessionViewModel::setMarkers(std::vector<Marker> markers)
{
	submit([this, markers = std::move(markers)] {
		markersM_ = markers;
		pinMarkersOnMachine();
	});
}

void SessionViewModel::pinMarkersOnMachine()
{
	if (s_ == nullptr) return;
	ce_session_greenzone_unpin_all(s_);
	for (const Marker &m : markersM_)
		if (m.keepState) ce_session_greenzone_pin(s_, m.frame, 1);
}

void SessionViewModel::onMachineWait(MachineJob job)
{
	auto done = std::make_shared<std::promise<void>>();
	std::future<void> ran = done->get_future();
	submit([this, job = std::move(job), done] {
		job(s_);
		done->set_value();
	});
	ran.wait();
}

int SessionViewModel::addMachineHooks(MachineJob frame, MachineJob closing)
{
	const int id = nextHook_++;
	submit([this, id, frame = std::move(frame), closing = std::move(closing)] { hooksM_.push_back({id, frame, closing}); });
	return id;
}

void SessionViewModel::removeMachineHooks(int id)
{
	auto done = std::make_shared<std::promise<void>>();
	std::future<void> gone = done->get_future();
	submit([this, id, done] {
		std::erase_if(hooksM_, [id](const Hooks &h) { return h.id == id; });
		done->set_value();
	});
	gone.wait();
}

void SessionViewModel::freeProjectOnMachine()
{
	if (projectM_ != nullptr) ce_project_free(projectM_);
	projectM_ = nullptr;
	projectPathM_.clear();
	logKeyM_.clear();
}

void SessionViewModel::openProjectOnMachine(const std::string &path, const std::string &package, bool keepInputs,
	ce_project *reuse)
{
	freeSessionOnMachine();
	runningM_ = false;
	auto fail = [this](const std::string &why) {
		later([this, why] { status.set(why); isOpen.set(false); });
	};

	/* reuse: the project already open, changed in memory (its settings) */
	const char *error = nullptr;
	ce_project *p = reuse != nullptr ? reuse : ce_project_open(path.c_str(), &error);
	if (p == nullptr)
	{
		fail("Could not open " + path + ": " + (error != nullptr ? error : "no reason given"));
		return;
	}
	if (reuse != nullptr) projectM_ = nullptr; /* taken over below, not freed */
	/* the files are looked for beside the project (more places, later) */
	const size_t slash = path.find_last_of("/\\");
	const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
	ce_project_resolve_dir(p, dir.c_str());
	if (ce_project_files_ok(p) == 0)
	{
		std::string missing;
		for (int32_t i = 0; i < ce_project_file_count(p); i++)
			if (ce_project_file_status(p, i) != 0)
				missing += std::string(missing.empty() ? "" : ", ") + ce_project_file_name(p, i) +
					(ce_project_file_status(p, i) == 1 ? " (not found)" : " (a different file)");
		ce_project_free(p);
		fail("The project's files are not all beside it: " + missing);
		return;
	}
	/* a package other than the one the project names opens, and says so */
	uint32_t flags = 0;
	std::string note;
	const std::string pinned = ce_project_core_sha1(p) != nullptr ? ce_project_core_sha1(p) : "";
	if (!pinned.empty())
	{
		const char *perr = nullptr;
		if (ce_package *pk = ce_package_open(package.c_str(), &perr))
		{
			const char *sha = ce_package_sha1(pk);
			if (sha == nullptr || pinned != sha)
			{
				flags |= CE_OPEN_ALLOW_CORE_MISMATCH;
				note = "; this is not the package the project was made with";
			}
			ce_package_free(pk);
		}
	}
	ce_session *s = ce_session_open_project(package.c_str(), p, nullptr, nullptr, nullptr, nullptr, 0, flags, &error);
	if (s == nullptr)
	{
		ce_project_free(p);
		fail(std::string("Could not open the project's machine: ") + (error != nullptr ? error : "no reason given"));
		return;
	}

	/* the movie: given to the session, which then reads and writes entries
	 * in its layout (the movie's LogKey), and read into the inputs */
	uint64_t len = 0;
	const char *text = ce_project_log_text(p, &len);
	ce_movie_log *log = ce_movie_log_new();
	if (text != nullptr && len > 0) ce_movie_log_parse(log, text, len);
	ce_session_movie_load(s, log);
	const ce_movie_log *held = ce_session_movie_log(s);
	const char *key = held != nullptr ? ce_movie_log_key(held) : nullptr;
	logKeyM_ = key != nullptr ? key : (ce_movie_log_key(log) != nullptr ? ce_movie_log_key(log) : "");
	const size_t axisCount = static_cast<size_t>(ce_session_axis_count(s));
	if (!keepInputs)
	{
		const int64_t n = ce_movie_log_count(log);
		std::vector<uint64_t> masks(static_cast<size_t>(n), 0);
		std::vector<int32_t> axes(axisCount * static_cast<size_t>(n), 0);
		int64_t unreadable = 0;
		for (int64_t i = 0; i < n; i++)
			if (ce_session_movie_entry_decode(s, ce_movie_log_entry(log, i), &masks[static_cast<size_t>(i)],
					axisCount ? &axes[static_cast<size_t>(i) * axisCount] : nullptr) != 0)
				unreadable++;
		std::vector<int32_t> neutral(axisCount);
		for (size_t i = 0; i < axisCount; i++) neutral[i] = ce_session_axis_neutral(s, static_cast<int64_t>(i));
		inputs_.setAxes(neutral);
		inputs_.assign(std::move(masks), std::move(axes));
		if (unreadable > 0) note += "; " + std::to_string(unreadable) + " frames of the movie could not be read";
	}
	ce_movie_log_free(log);

	if (!keepInputs)
	{
		std::vector<Marker> markers;
		for (int32_t i = 0; i < ce_project_marker_count(p); i++)
		{
			const char *text = ce_project_marker_text(p, i);
			markers.push_back({ce_project_marker_frame(p, i), text != nullptr ? text : "", ce_project_marker_keep_state(p, i) != 0});
		}
		markersM_ = markers;
		later([this, markers] { markersLoaded.emit(markers); });
	}
	if (projectM_ != nullptr && projectM_ != p) ce_project_free(projectM_);
	projectM_ = p;
	projectPathM_ = path;
	package_ = package;
	game_.clear();
	const std::string title = ce_project_title(p) != nullptr ? ce_project_title(p) : "";
	adoptOnMachine(s, package, (title.empty() ? path : title) + note, !keepInputs);
	later([this, path, title] { projectPath.set(path); projectTitle.set(title); });
}

void SessionViewModel::saveProjectOnMachine(const std::string &to)
{
	if (projectM_ == nullptr || s_ == nullptr) return;
	const std::string path = to.empty() ? projectPathM_ : to;
	if (ce_session_button_count(s_) > 64)
	{
		later([this] { status.set("Not saved: this machine has more than 64 buttons, which the prototype does not keep"); });
		return;
	}
	ce_movie_log *log = ce_movie_log_new();
	if (!logKeyM_.empty()) ce_movie_log_set_key(log, logKeyM_.c_str());
	const size_t axisCount = inputs_.axisCount();
	std::vector<int32_t> axes(axisCount);
	std::vector<char> line(256);
	const int64_t n = inputs_.size();
	for (int64_t f = 0; f < n; f++)
	{
		if (axisCount) inputs_.axesAt(f, axes.data());
		const int32_t need = ce_session_movie_entry_encode(s_, inputs_.at(f), axisCount ? axes.data() : nullptr,
			line.data(), static_cast<int32_t>(line.size()));
		if (need >= static_cast<int32_t>(line.size()))
		{
			line.resize(static_cast<size_t>(need) + 1);
			ce_session_movie_entry_encode(s_, inputs_.at(f), axisCount ? axes.data() : nullptr, line.data(),
				static_cast<int32_t>(line.size()));
		}
		ce_movie_log_add(log, line.data());
	}
	uint64_t len = 0;
	const char *text = ce_movie_log_serialize(log, 0, &len);
	ce_project_set_log_text(projectM_, text, len);
	for (int32_t i = ce_project_marker_count(projectM_) - 1; i >= 0; i--) ce_project_marker_remove(projectM_, i);
	for (const Marker &m : markersM_) ce_project_marker_add(projectM_, m.frame, m.text.c_str(), m.keepState ? 1 : 0);
	ce_movie_log_free(log);
	const char *error = nullptr;
	if (ce_project_save(projectM_, path.c_str(), &error) != 0)
	{
		const std::string why = error != nullptr ? error : "no reason given";
		later([this, path, why] { status.set("Could not save " + path + ": " + why); });
		return;
	}
	projectPathM_ = path;
	later([this, path, n] {
		projectPath.set(path);
		status.set("Saved " + path + " (" + std::to_string(n) + " frames)");
	});
}

std::vector<uint8_t> SessionViewModel::readDomain(const std::string &name)
{
	auto done = std::make_shared<std::promise<std::vector<uint8_t>>>();
	std::future<std::vector<uint8_t>> result = done->get_future();
	submit([this, name, done] {
		std::vector<uint8_t> bytes;
		if (s_ != nullptr)
			for (int32_t i = 0; i < ce_session_domain_count(s_); i++)
				if (name == ce_session_domain_name(s_, i))
				{
					bytes.resize(static_cast<size_t>(ce_session_domain_size(s_, i)));
					ce_session_domain_read(s_, i, 0, bytes.data(), static_cast<int64_t>(bytes.size()));
					break;
				}
		done->set_value(std::move(bytes));
	});
	return result.get();
}

void SessionViewModel::adoptOnMachine(ce_session *s, const std::string &package, const std::string &label, bool inputsReplaced)
{
	s_ = s;
	frameM_ = 0;
	greenEndM_ = 0;
	/* a history of the machine's states, so a seek replays from the nearest
	 * one rather than from power-on (a setting, later) */
	ce_session_greenzone_enable(s, uint64_t{1} << 30);
	pinMarkersOnMachine();

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

	/* axes rest at their neutral until a frame moves them */
	std::vector<int32_t> neutral(static_cast<size_t>(ce_session_axis_count(s)));
	for (size_t i = 0; i < neutral.size(); i++) neutral[i] = ce_session_axis_neutral(s, static_cast<int64_t>(i));
	if (inputs_.axisCount() != neutral.size()) inputs_.setAxes(neutral);

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

	const int64_t frames = inputs_.size();
	later([this, names, mnemonics, keybinds, coreText, systemText, rate, label, inputsReplaced, frames] {
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
		opens.set(opens.get() + 1);
		char hz[32];
		std::snprintf(hz, sizeof hz, "%.3f", rate);
		status.set("Opened " + label + " (" + hz + " frames a second, " + std::to_string(bindings_.boundKeys()) +
			" keys bound" + (bound ? "" : "; the package's bindings are not readable: " + why) + ")");
		buttonsChanged.emit();
		if (inputsReplaced) inputsReset.emit();
		(void)frames;
	});
}

void SessionViewModel::closeOnMachine()
{
	if (s_ == nullptr) return;
	freeSessionOnMachine();
	freeProjectOnMachine();
	runningM_ = false;
	later([this] {
		isOpen.set(false);
		running.set(false);
		fps.set(0.0);
		projectPath.set("");
		projectTitle.set("");
		status.set("Closed");
	});
}

void SessionViewModel::advanceOnMachine()
{
	const int64_t f = frameM_;
	const int64_t size = inputs_.size();
	stepOnMachine(true, true);
	/* the step reports what it changed; here the frames added and the rate */
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
	report(-1, -1, f >= size ? size : -1, f >= size ? f : -1, measured);
}

int32_t SessionViewModel::stepOnMachine(bool live, bool render)
{
	const int64_t f = frameM_;
	uint64_t held = 0;
	if (live)
	{
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
		held = held_.load() | padMask;
	}
	const int64_t size = inputs_.size();
	const uint64_t logged = inputs_.at(f);
	const uint64_t mask = logged | held;
	bool changed = false;
	if (f >= size)
		inputs_.set(f, mask);
	else if (mask != logged)
	{
		/* a key held over a frame that has inputs is an edit of it: the
		 * states after it no longer hold */
		inputs_.set(f, mask);
		ce_session_greenzone_invalidate(s_, f);
		greenEndM_ = std::min(greenEndM_, f);
		changed = true;
	}

	const size_t axisCount = inputs_.axisCount();
	if (axisCount > 0)
	{
		axesBuf_.resize(axisCount);
		inputs_.axesAt(f, axesBuf_.data());
		for (size_t i = 0; i < axisCount; i++) ce_session_set_axis(s_, static_cast<int32_t>(i), axesBuf_[i]);
	}

	ce_session_greenzone_before_advance(s_);
	const int32_t lag = ce_session_frame_advance(s_, mask, render ? 1 : 0);
	frameM_++;
	ce_session_greenzone_capture(s_, frameM_, nullptr, 0);
	greenEndM_ = std::max(greenEndM_, frameM_);
	for (const Hooks &h : hooksM_)
		if (h.frame) h.frame(s_);
	if (inputs_.setLag(f, lag > 0)) changed = true;

	const char *death = ce_session_guest_death(s_);
	if (death != nullptr && *death != '\0')
	{
		runningM_ = false;
		const std::string why = death;
		later([this, why] { running.set(false); status.set("The machine stopped: " + why); });
	}

	if (live && soundM_)
	{
		int32_t pairs = 0;
		const int16_t *samples = ce_session_audio(s_, &pairs);
		soundM_->play(samples, pairs);
	}

	if (render)
	{
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
	}
	if (changed)
	{
		std::lock_guard<std::mutex> lock(pendingM_);
		changedFirst_ = changedFirst_ < 0 ? f : std::min(changedFirst_, f);
		changedLast_ = std::max(changedLast_, f);
	}
	return lag;
}

void SessionViewModel::seekOnMachine(int64_t target)
{
	if (s_ == nullptr || target < 0 || target == frameM_) return;
	const auto start = Clock::now();
	const int64_t from = frameM_;
	/* the picture of `target` is drawn by running its last frame, so the
	 * machine goes back to a state at or before target - 1 */
	const int64_t want = target > 0 ? target - 1 : 0;
	const int64_t stored = ce_session_greenzone_nearest(s_, want);
	const bool ahead = target > frameM_ && (stored < 0 || stored <= frameM_);
	if (!ahead)
	{
		if (stored < 0 || ce_session_greenzone_restore(s_, stored) != 0)
		{
			const std::string why = std::string("Could not go back to frame ") + std::to_string(target) + ": " +
				(ce_session_last_error(s_) != nullptr ? ce_session_last_error(s_) : "no state stored before it");
			later([this, why] { status.set(why); });
			return;
		}
		frameM_ = stored;
	}
	int64_t ran = 0;
	while (frameM_ < target && s_ != nullptr)
	{
		stepOnMachine(false, frameM_ == target - 1);
		ran++;
	}
	const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	later([this, ms, ran] {
		lastSeekMs.set(ms);
		lastSeekFrames.set(ran);
	});
	(void)from;
	report(-1, -1, -1, -1, -1);
}

void SessionViewModel::edited(int64_t first)
{
	submit([this, first] {
		if (s_ == nullptr) return;
		ce_session_greenzone_invalidate(s_, first);
		greenEndM_ = std::min(greenEndM_, first);
		if (frameM_ > first)
		{
			/* the machine is past the edit: go back to it, and with
			 * autoRestore run on to where it was with the new inputs */
			const int64_t was = frameM_;
			seekOnMachine(first);
			if (autoRestoreM_.load()) seekOnMachine(was);
		}
		report(-1, -1, -1, -1, -1);
	});
}

void SessionViewModel::report(int64_t cf, int64_t cl, int64_t af, int64_t al, double fpsNow)
{
	bool post = false;
	{
		std::lock_guard<std::mutex> lock(pendingM_);
		pendingFrame_ = frameM_;
		pendingGreenEnd_ = greenEndM_;
		pendingStates_ = s_ != nullptr ? ce_session_greenzone_count(s_) : 0;
		if (fpsNow >= 0) pendingFps_ = fpsNow;
		if (soundM_)
		{
			pendingQueued_ = soundM_->queued();
			pendingDropped_ = soundM_->dropped();
		}
		if (cf >= 0)
		{
			changedFirst_ = changedFirst_ < 0 ? cf : std::min(changedFirst_, cf);
			changedLast_ = std::max(changedLast_, cl);
		}
		if (af >= 0)
		{
			appendedFirst_ = appendedFirst_ < 0 ? af : std::min(appendedFirst_, af);
			appendedLast_ = std::max(appendedLast_, al);
		}
		if (!tickPosted_) post = tickPosted_ = true;
	}
	if (post) later([this] { tick(); });
}

void SessionViewModel::tick()
{
	int64_t f, cf, cl, af, al, green, states;
	double measured;
	int64_t queued, dropped;
	{
		std::lock_guard<std::mutex> lock(pendingM_);
		tickPosted_ = false;
		f = pendingFrame_;
		measured = pendingFps_;
		cf = changedFirst_, cl = changedLast_, af = appendedFirst_, al = appendedLast_;
		queued = pendingQueued_, dropped = pendingDropped_;
		green = pendingGreenEnd_, states = pendingStates_;
		pendingFps_ = -1;
		changedFirst_ = changedLast_ = appendedFirst_ = appendedLast_ = -1;
	}
	frame.set(f);
	if (measured >= 0) fps.set(measured);
	soundQueued.set(queued);
	soundDropped.set(dropped);
	greenEnd.set(green);
	greenzoneStates.set(states);
	if (cf >= 0) inputsChanged.emit(cf, cl);
	if (af >= 0) inputsAppended.emit(af, al);
	screenChanged.emit();
}

} // namespace chimera::vm
