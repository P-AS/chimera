#include "chimera/vm/session_vm.hpp"

#include "chimera/vm/branch_state.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <iterator>

#include "chimera/engine.h"
#include "chimera/vm/firmware.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/settings_vm.hpp"
#include "chimera/vm/suggestion.hpp"

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
	expose("speedPercent", "how fast the machine runs, a percent of its own rate (1 to 6400)", speedPercent, true);
	expose("unthrottled", "the machine runs as fast as it can", unthrottled, true);
	expose("soundOn", "the machine's sound is played", soundOn, true);
	expose("volume", "the sound's volume, 0 to 100", volume, true);
	speedWatch_ = speedPercent.changed.connect([this] {
		speedM_ = std::clamp<int64_t>(speedPercent.get(), 1, 6400);
	});
	unthrottledWatch_ = unthrottled.changed.connect([this] { unthrottledM_ = unthrottled.get(); });
	soundOnWatch_ = soundOn.changed.connect([this] { soundOnM_ = soundOn.get(); });
	volumeWatch_ = volume.changed.connect([this] { volumeM_ = std::clamp<int64_t>(volume.get(), 0, 100); });
	/* Chimera's speeds, a step at a time */
	auto speedStep = [this](int direction) {
		static const int64_t steps[] = {1, 3, 6, 12, 25, 50, 75, 100, 150, 200, 300, 400, 800, 1600, 3200, 6400};
		const int64_t now = speedPercent.get();
		int64_t next = now;
		if (direction > 0)
		{
			for (int64_t s : steps)
				if (s > now)
				{
					next = s;
					break;
				}
		}
		else
			for (int i = static_cast<int>(std::size(steps)) - 1; i >= 0; i--)
				if (steps[i] < now)
				{
					next = steps[i];
					break;
				}
		speedPercent.set(next);
		status.set("Speed: " + std::to_string(next) + "%");
	};
	command("speedUp", "the next of Chimera's speeds up", {}, [speedStep](const Args &, std::string &) -> Value {
		speedStep(1);
		return {};
	});
	command("speedDown", "the next of Chimera's speeds down", {}, [speedStep](const Args &, std::string &) -> Value {
		speedStep(-1);
		return {};
	});
	command("speedReset", "the machine's own speed, 100%", {}, [this](const Args &, std::string &) -> Value {
		speedPercent.set(100);
		status.set("Speed: 100%");
		return {};
	});
	command("toggleThrottle", "as fast as it can, or at its speed again", {}, [this](const Args &, std::string &) -> Value {
		unthrottled.set(!unthrottled.get());
		status.set(unthrottled.get() ? "Unthrottled" : "Throttled");
		return {};
	});
	command("toggleSound", "the sound on or off", {}, [this](const Args &, std::string &) -> Value {
		soundOn.set(!soundOn.get());
		status.set(soundOn.get() ? "Sound on" : "Sound off");
		return {};
	});
	command("volumeUp", "the volume up by ten", {}, [this](const Args &, std::string &) -> Value {
		volume.set(std::min<int64_t>(100, volume.get() + 10));
		status.set("Volume " + std::to_string(volume.get()));
		return {};
	});
	command("volumeDown", "the volume down by ten", {}, [this](const Args &, std::string &) -> Value {
		volume.set(std::max<int64_t>(0, volume.get() - 10));
		status.set("Volume " + std::to_string(volume.get()));
		return {};
	});
	expose("showFps", "the speed is shown over the picture", showFps, true);
	expose("showFrameCounter", "the frame counter is shown over the picture", showFrameCounter, true);
	expose("showLagCounter", "the lag counter is shown over the picture", showLagCounter, true);
	expose("showInput", "the input of the frame just run is shown over the picture", showInput, true);
	expose("fpsText", "the speed as shown: \"60 fps\", with >> fast forwarding, << rewinding", fpsText);
	expose("frameCounterText", "the frame counter as shown: the frame, the last frame with input, (Finished) past it", frameCounterText);
	expose("inputText", "the input of the frame just run, as the input display writes it", inputText);
	expose("lagCount", "lag frames so far", lagCount);
	expose("lagFrame", "the frame just run was a lag frame", lagFrame);
	/* turned on, a counter says at once what it has to say, counted afresh:
	 * off, it heard of no change */
	for (Property<bool> *shown : {&showFps, &showFrameCounter, &showLagCounter, &showInput})
		overlayWatches_.push_back(shown->changed.connect([this] {
			lastInputFor_ = -1;
			lagsTo_ = lags_ = 0;
			updateOverlay(frame.get(), -1);
		}));
	expose("catchingPad", "waiting for a gamepad input to be pressed (catchPad)", catchingPad);
	expose("caughtPad", "the gamepad input caught, by Chimera's name; \"\" while waiting", caughtPad);
	expose("sound", "\"Sound on\", or why there is none", sound);
	expose("soundQueued", "sample pairs sent to the speakers", soundQueued);
	expose("soundDropped", "sample pairs dropped because the machine ran ahead of the speakers", soundDropped);
	expose("greenEnd", "frames before this one were run with the inputs they have now", greenEnd);
	expose("greenzoneStates", "machine states the greenzone holds", greenzoneStates);
	expose("rerecords", "how many times the movie was changed over work already done (saved with the project)", rerecords);
	expose("autoRestore", "after an edit before the current frame, run on to where the machine was", autoRestore, true);
	expose("greenzonePeriod", "which frames the greenzone stores: 1 every frame, N one in N, 0 none (saved with the project)",
		greenzonePeriod, true);
	expose("greenzoneEvery", "the N of storing every N frames, 2 to 999", greenzoneEvery, true);
	command("cycleGreenzone", "every frame, then every N frames, then none, then every frame again", {},
		[this](const Args &, std::string &) -> Value {
			const int64_t now = greenzonePeriod.get();
			greenzonePeriod.set(now == 1 ? std::clamp<int64_t>(greenzoneEvery.get(), 2, 999) : now == 0 ? 1 : 0);
			return greenzonePeriod.get();
		});
	periodWatch_ = greenzonePeriod.changed.connect([this] {
		if (greenzonePeriod.get() < 0) greenzonePeriod.set(0);
		const int64_t p = greenzonePeriod.get();
		periodM_.store(p);
		submit([this, p] {
			if (s_ != nullptr) ce_session_greenzone_capture_period(s_, p);
		});
	});
	expose("lastSeekMs", "how long the last seek took, in milliseconds", lastSeekMs);
	expose("lastSeekFrames", "how many frames the last seek ran", lastSeekFrames);
	expose("projectPath", "the open project's file, \"\" when a game was opened without one or the project is not saved yet", projectPath);
	expose("hasProject", "a project is open, saved or not yet", hasProject);
	expose("projectTitle", "the open project's title", projectTitle);
	expose("gameName", "the game's name: its file's, without the kind", gameName);
	expose("recentProjects", "projects opened, newest first, a line each", recentProjects, true);
	expose("chooseFor", "a game waiting for its core to be chosen", chooseFor);
	expose("choices", "the cores the waiting game could be opened with, a line each", choices);
	picture("screen", "the machine's picture", screen, screenChanged);
	autoRestoreWatch_ = autoRestore.changed.connect([this] { autoRestoreM_.store(autoRestore.get()); });
	expose("cacheFolder", "where what a project can make again is kept (its branches' states), a folder per project", cacheFolder, true);
	cacheFolderWatch_ = cacheFolder.changed.connect([this] {
		submit([this, folder = cacheFolder.get()] { cacheFolderM_ = folder; });
	});

	command("open", "opens a core package with a game file", {"package", "game"},
		[this](const Args &a, std::string &error) -> Value {
			std::string package, game;
			if (!arg(a, 0, package, error) || !arg(a, 1, game, error)) return {};
			status.set("Opening " + game + "...");
			/* another game starts from its package's own settings */
			submit([this, package, game] {
				settingsM_.clear();
				markersM_.clear();
				branchesM_.clear();
				later([this] { markersLoaded.emit({}); branchesLoaded.emit({}); });
				openOnMachine(package, game, false);
			});
			return {};
		});
	command("openGame", "opens a game with the installed core that takes its kind of file; when several do, they are "
		"offered (chooseFor, choices) and chooseCore picks one", {"game"},
		[this](const Args &a, std::string &error) -> Value {
			std::string game;
			if (!arg(a, 0, game, error)) return {};
			const size_t dot = game.find_last_of('.');
			std::string ext = dot == std::string::npos ? "" : game.substr(dot + 1);
			for (char &ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			offered_ = byExtension_ ? byExtension_(ext) : decltype(offered_){};
			if (offered_.empty())
			{
				error = "no installed core takes ." + ext + " files";
				return {};
			}
			if (offered_.size() == 1)
			{
				run("open", {offered_[0].second, game}, error);
				return offered_[0].first;
			}
			std::string names;
			for (const auto &[name, path] : offered_) names += (names.empty() ? "" : "\n") + name;
			choices.set(names);
			chooseFor.set(game);
			return {};
		});
	command("chooseCore", "opens the game waiting for a core with the one at this place in choices; -1 opens nothing", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = 0;
			if (!arg(a, 0, i, error)) return {};
			if (chooseFor.get().empty() || i < -1 || i >= static_cast<int64_t>(offered_.size()))
			{
				error = "no such choice";
				return {};
			}
			const std::string game = chooseFor.get();
			chooseFor.set("");
			choices.set("");
			if (i == -1) return {};
			run("open", {offered_[static_cast<size_t>(i)].second, game}, error);
			return offered_[static_cast<size_t>(i)].first;
		});
	command("openProject", "opens a Chimera project; its files are looked for beside it, its core among the installed "
		"ones unless a package is given", {"project", "package"}, [this](const Args &a, std::string &error) -> Value {
			std::string project, package;
			if (!arg(a, 0, project, error)) return {};
			if (a.size() > 1 && !arg(a, 1, package, error)) return {};
			status.set("Opening " + project + "...");
			submit([this, project, package] { openProjectOnMachine(project, package, false); });
			return {};
		});
	command("saveProject", "saves the open project with its inputs, to its own file or to the one given",
		{"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!a.empty() && !arg(a, 0, path, error)) return {};
			if (!hasProject.get())
			{
				error = "no project is open";
				return {};
			}
			if (path.empty() && projectPath.get().empty())
			{
				error = "this project has not been saved yet: say where it goes";
				return {};
			}
			submit([this, path] { saveProjectOnMachine(path); });
			return {};
		}, &isOpen);
	command("saveAsProject", "makes the open game a project, saved at the path: its core, file, settings, firmware and "
		"movie; the game's file is looked for beside the project when it is opened again", {"path", "title"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path, title;
			if (!arg(a, 0, path, error)) return {};
			if (a.size() > 1 && !arg(a, 1, title, error)) return {};
			if (hasProject.get())
			{
				error = "a project is open: Save Project As keeps it";
				return {};
			}
			submit([this, path, title] { newProjectOnMachine(path, title); });
			return {};
		}, &isOpen);
	command("catchPad", "waits for the next gamepad input pressed, a game open or not, and puts its name in caughtPad",
		{}, [this](const Args &, std::string &) -> Value {
			caughtPad.set("");
			catchingPad.set(true);
			submit([this] {
				if (!padsM_) padsM_ = std::make_unique<Gamepads>();
				/* what is held already is not what is pressed */
				catchHeldM_.clear();
				padsM_->poll(catchHeldM_);
				catchingM_ = true;
				catchDue_ = Clock::now() + std::chrono::milliseconds(16);
			});
			return {};
		});
	command("stopCatchPad", "stops waiting for a gamepad input", {}, [this](const Args &, std::string &) -> Value {
		catchingPad.set(false);
		submit([this] { catchingM_ = false; });
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
	command("savePicture", "writes the picture now on screen as a TGA file, as chimera-run does", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			Picture::Frame f;
			if (!screen.takeIfNewer(UINT64_MAX, f) || f.width <= 0 || f.height <= 0)
			{
				error = "there is no picture yet";
				return {};
			}
			/* uncompressed 32-bit, rows bottom-up: the engine's BGRA is what a TGA stores */
			std::vector<uint8_t> out(18 + static_cast<size_t>(f.width) * f.height * 4, 0);
			out[2] = 2;
			out[12] = static_cast<uint8_t>(f.width & 0xFF);
			out[13] = static_cast<uint8_t>((f.width >> 8) & 0xFF);
			out[14] = static_cast<uint8_t>(f.height & 0xFF);
			out[15] = static_cast<uint8_t>((f.height >> 8) & 0xFF);
			out[16] = 32;
			out[17] = 8;
			const size_t row = static_cast<size_t>(f.width) * 4;
			for (int32_t y = 0; y < f.height; y++)
				std::memcpy(&out[18 + static_cast<size_t>(f.height - 1 - y) * row], &f.pixels[static_cast<size_t>(y) * f.width], row);
			std::ofstream file(path, std::ios::binary);
			file.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
			if (!file)
			{
				error = "cannot write " + path;
				return {};
			}
			return static_cast<int64_t>(f.serial);
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
				/* a project opens again as it is in memory, its movie and
				 * markers as they are now; a game with the movie it has */
				if (projectM_ != nullptr) openProjectOnMachine(projectPathM_, package_, true, projectM_);
				else if (!package_.empty()) openOnMachine(package_, game_, true);
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

void SessionViewModel::holdFrameAdvance(bool down)
{
	submit([this, down] {
		if (s_ == nullptr) return;
		if (down)
		{
			runningM_ = false;
			advanceOnMachine();
			later([this] { running.set(false); });
			holdM_ = Hold::Advance;
			holdSince_ = Clock::now();
			holdRunning_ = false;
		}
		else if (holdM_ == Hold::Advance)
		{
			holdM_ = Hold::None;
			if (holdRunning_)
			{
				runningM_ = false;
				holdRunning_ = false;
				later([this] { running.set(false); });
			}
		}
	});
}

void SessionViewModel::holdRewind(bool down)
{
	rewindHeld_ = down;
	submit([this, down] {
		if (s_ == nullptr) return;
		if (down)
		{
			runningM_ = false;
			later([this] { running.set(false); });
			if (frameM_ > 0) seekOnMachine(frameM_ - 1);
			holdM_ = Hold::Rewind;
			/* the first step back at once, the next after a moment */
			due_ = Clock::now() + std::chrono::milliseconds(500);
		}
		else if (holdM_ == Hold::Rewind)
			holdM_ = Hold::None;
	});
}

void SessionViewModel::holdFastForward(bool down)
{
	fastHeld_ = down;
	submit([this, down] {
		fastM_ = down;
		if (!down) due_ = Clock::now();
	});
}

void SessionViewModel::setBindingTexts(std::vector<std::string> texts)
{
	texts.resize(buttonNames_.size());
	bindings_.setTexts(texts);
	/* the keys held now hold what they hold now */
	uint64_t mask = 0;
	for (const auto &[k, isDown] : keysDown_)
		if (isDown)
			for (int b : bindings_.buttonsOf(k))
				if (b < 64) mask |= uint64_t{1} << b;
	held_.store(mask);
	submit([this, texts] { bindingsM_.setTexts(texts); });
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
		bool advance = false, runOn = false, back = false, catchPoll = false;
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
				/* a binding waiting for a gamepad input: the pads looked at
				 * sixty times a second, a game open or not */
				if (catchingM_ && Clock::now() >= catchDue_)
				{
					catchPoll = true;
					break;
				}
				/* frame advance held half a second runs on */
				if (s_ != nullptr && holdM_ == Hold::Advance && !runningM_)
				{
					const auto when = holdSince_ + std::chrono::milliseconds(500);
					if (Clock::now() >= when) { runOn = true; break; }
					cv_.wait_until(lock, when);
					continue;
				}
				/* rewind held goes back a frame a frame */
				if (s_ != nullptr && holdM_ == Hold::Rewind)
				{
					if (Clock::now() >= due_) { back = true; break; }
					cv_.wait_until(lock, due_);
					continue;
				}
				if (runningM_ && s_ != nullptr)
				{
					if (Clock::now() >= due_) { advance = true; break; }
					cv_.wait_until(lock, catchingM_ ? std::min(due_, catchDue_) : due_);
				}
				else if (catchingM_)
					cv_.wait_until(lock, catchDue_);
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
		if (catchPoll)
		{
			catchPollOnMachine();
			continue;
		}
		if (runOn)
		{
			runningM_ = true;
			holdRunning_ = true;
			due_ = Clock::now();
			later([this] { running.set(true); });
			continue;
		}
		if (back)
		{
			if (frameM_ > 0) seekOnMachine(frameM_ - 1);
			due_ += period_;
			if (Clock::now() - due_ > 4 * period_) due_ = Clock::now();
			continue;
		}
		if (advance)
		{
			advanceOnMachine();
			/* fast forward: the next frame at once */
			/* fast forward or unthrottled: the next frame at once; else at
			 * the speed asked for */
			due_ += fastM_ || unthrottledM_ ? Clock::duration::zero()
											 : std::chrono::duration_cast<Clock::duration>(period_ * (100.0 / static_cast<double>(speedM_.load())));
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

void SessionViewModel::openOnMachine(const std::string &package, const std::string &game, bool keepMovie)
{
	const std::string movie = keepMovie && s_ != nullptr ? movieTextOnMachine() : std::string();
	const uint64_t counted = keepMovie && s_ != nullptr ? ce_session_rerecords(s_) : 0;
	freeSessionOnMachine();
	runningM_ = false;

	std::ifstream in(game, std::ios::binary);
	if (!in)
	{
		later([this, game] { status.set("Could not read " + game); isOpen.set(false); });
		return;
	}
	const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	/* the firmware it needs: the game goes in the package's first slot, for
	 * the conditions that ask about the files */
	Firmware fw;
	{
		std::string slotId = "rom";
		const size_t dot = game.find_last_of('.');
		std::string ext = dot == std::string::npos ? "" : game.substr(dot + 1);
		for (char &ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		const char *perr = nullptr;
		if (ce_package *pk = ce_package_open(package.c_str(), &perr))
		{
			uint64_t len = 0;
			json::Json slots;
			std::string jerr;
			if (const uint8_t *d = ce_package_entry(pk, "file_slots.json", &len);
				d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), slots, jerr))
				if (const json::Json *list = slots.find("slots"); list != nullptr && !list->items.empty())
				{
					/* the slot that takes this kind of file, else the first */
					if (const json::Json *id = list->items[0].find("id")) slotId = id->text;
					for (const json::Json &slot : list->items)
					{
						bool takes = false;
						if (const json::Json *formats = slot.find("formats"))
							for (const json::Json &f : formats->items)
							{
								std::string e = f.text;
								for (char &ch : e) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
								takes = takes || e == ext;
							}
						if (takes)
						{
							if (const json::Json *id = slot.find("id")) slotId = id->text;
							break;
						}
					}
				}
			ce_package_free(pk);
		}
		gameSlotM_ = slotId;
		const size_t slash = game.find_last_of("/\\");
		const std::string base = slash == std::string::npos ? game : game.substr(slash + 1);
		json::Json slotMap;
		slotMap.kind = json::Json::Kind::Object;
		json::Json files;
		files.kind = json::Json::Kind::Array;
		files.items.push_back(json::Json::string(base));
		slotMap.members.emplace_back(slotId, files);
		std::string why;
		if (!firmwareOnMachine(package, json::write(slotMap), settingsM_.empty() ? "{}" : settingsM_, "", game, {}, fw, why))
		{
			later([this, why] { status.set(why); isOpen.set(false); });
			return;
		}
	}
	std::vector<const char *> fwIds;
	std::vector<const uint8_t *> fwData;
	std::vector<uint64_t> fwLens;
	for (size_t i = 0; i < fw.ids.size(); i++)
	{
		fwIds.push_back(fw.ids[i].c_str());
		fwData.push_back(fw.data[i].data());
		fwLens.push_back(fw.data[i].size());
	}
	firmwarePinsM_ = fw.pins;
	const char *error = nullptr;
	ce_session *s = ce_session_open(package.c_str(), bytes.data(), bytes.size(), game.c_str(),
		settingsM_.empty() ? nullptr : settingsM_.c_str(), fwIds.data(), fwData.data(), fwLens.data(),
		static_cast<int32_t>(fwIds.size()), nullptr, nullptr, nullptr, nullptr, 0, &error);
	if (s == nullptr)
	{
		const std::string why = error != nullptr ? error : "no reason given";
		later([this, why] { status.set("Could not open: " + why); isOpen.set(false); });
		return;
	}
	freeProjectOnMachine();
	later([this] {
		projectPath.set("");
		projectTitle.set("");
		hasProject.set(false);
	});
	/* the movie: the one it had when started again, else an empty one */
	loadMovieOnMachine(s, movie);
	/* a game alone starts storing every frame, as a new project does */
	if (!keepMovie)
	{
		periodM_.store(1);
		later([this] { greenzonePeriod.set(1); });
	}
	ce_session_set_rerecords(s, counted);
	adoptOnMachine(s, package, game, true);
	{
		std::string stem = std::filesystem::path(game).stem().string();
		later([this, stem] { gameName.set(stem); });
	}
	package_ = package;
	game_ = game;
}

void SessionViewModel::freeSessionOnMachine()
{
	if (s_ == nullptr) return;
	for (const Hooks &h : hooksM_)
		if (h.closing) h.closing(s_);
	inputs_.bind(nullptr);
	/* a greenzone being written is finished before its session goes */
	ce_session_history_save_wait(s_);
	ce_session_free(s_);
	s_ = nullptr;
	if (!runningMarkM_.empty())
	{
		std::error_code ec;
		std::filesystem::remove(runningMarkM_, ec);
		runningMarkM_.clear();
	}
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
		else openOnMachine(package_, game_, true);
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

void SessionViewModel::setBranches(std::vector<Branch> branches)
{
	submit([this, branches = std::move(branches)] { branchesM_ = branches; });
}

void SessionViewModel::loadMovie(std::string text, int64_t target)
{
	submit([this, text, target] {
		if (s_ == nullptr) return;
		runningM_ = false;
		later([this] { running.set(false); });
		replaceMovieOnMachine(text);
		seekOnMachine(target);
		later([this] { inputsReset.emit(); });
		report(-1, -1, -1, -1, -1);
	});
}

int64_t SessionViewModel::replaceMovieOnMachine(const std::string &text)
{
	/* where the two movies part: frames before it keep their green */
	ce_movie_log *now = ce_movie_log_new(), *next = ce_movie_log_new();
	const std::string current = movieTextOnMachine();
	if (!current.empty()) ce_movie_log_parse(now, current.data(), current.size());
	if (!text.empty()) ce_movie_log_parse(next, text.data(), text.size());
	const int64_t from = ce_movie_log_divergent_point(now, next);
	ce_session_movie_load(s_, next);
	ce_movie_log_free(now);
	ce_movie_log_free(next);
	if (from >= 0) greenEndM_ = std::min(greenEndM_, from);
	return from;
}

std::filesystem::path SessionViewModel::projectCacheOnMachine() const
{
	if (projectM_ == nullptr || cacheFolderM_.empty()) return {};
	const char *id = ce_project_id(projectM_);
	if (id == nullptr || *id == '\0') return {};
	return std::filesystem::path(cacheFolderM_) / id;
}

std::filesystem::path SessionViewModel::branchDirOnMachine() const
{
	const std::filesystem::path dir = projectCacheOnMachine();
	return dir.empty() ? dir : dir / "Branches";
}

std::string SessionViewModel::machineIdOnMachine()
{
	if (packageSha1M_.empty())
	{
		const char *perr = nullptr;
		if (ce_package *pk = ce_package_open(package_.c_str(), &perr))
		{
			if (const char *sha = ce_package_sha1(pk)) packageSha1M_ = sha;
			ce_package_free(pk);
		}
	}
	/* the engine's rule, for a project; a game alone keeps nothing, but
	 * the same three things name its machine */
	if (projectM_ != nullptr)
	{
		char id[41];
		ce_project_machine_id(projectM_, packageSha1M_.c_str(), id);
		return id;
	}
	return branch_state::fingerprint(packageSha1M_ + "\n" + settingsOnMachine() + "\n" + slotsOnMachine());
}

void SessionViewModel::startProjectSessionOnMachine(bool loadHistory)
{
	namespace fs = std::filesystem;
	const fs::path dir = projectCacheOnMachine();
	if (dir.empty() || s_ == nullptr) return;
	std::error_code ec;
	fs::create_directories(dir, ec);
	const fs::path mark = dir / "session.running", history = dir / "history.bin";
	if (fs::exists(mark, ec))
	{
		/* the last session did not end: what it wrote may be half a file,
		 * and a machine that killed it may have stored it */
		fs::remove(history, ec);
	}
	else if (loadHistory && fs::exists(history, ec) &&
			 ce_session_history_load(s_, branch_state::utf8(history).c_str(), machineIdOnMachine().c_str()) == 0)
	{
		const int64_t newest = ce_session_greenzone_nearest(s_, ce_session_movie_length(s_));
		greenEndM_ = std::max<int64_t>(0, newest);
		report(-1, -1, -1, -1, -1);
	}
	std::ofstream(mark) << "a session of this project is running\n";
	runningMarkM_ = mark;
}

void SessionViewModel::keepHistoryOnMachine()
{
	namespace fs = std::filesystem;
	const fs::path dir = projectCacheOnMachine();
	if (dir.empty() || s_ == nullptr) return;
	/* A machine a GPU drew makes states good only in the session that
	 * made them, unless its core says they outlive it; a machine that died
	 * cannot vouch for anything it stored. */
	bool survives = ce_session_gpu_drew(s_) == 0;
	if (!survives)
	{
		const char *perr = nullptr;
		if (ce_package *pk = ce_package_open(package_.c_str(), &perr))
		{
			uint64_t len = 0;
			json::Json cfg;
			std::string jerr;
			if (const uint8_t *d = ce_package_entry(pk, "waterbox.config", &len);
				d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, jerr))
				if (const json::Json *video = cfg.find("video"))
					if (const json::Json *keep = video->find("greenzoneSurvivesReload")) survives = keep->boolean;
			ce_package_free(pk);
		}
	}
	std::error_code ec;
	fs::create_directories(dir, ec);
	const fs::path history = dir / "history.bin";
	if (survives && !diedM_)
		ce_session_history_save_later(s_, branch_state::utf8(history).c_str(), machineIdOnMachine().c_str());
	else
	{
		ce_session_history_save_wait(s_);
		fs::remove(history, ec);
	}
}

void SessionViewModel::makeBranch(std::string name, std::string time, std::function<void(Branch, std::string)> done)
{
	submit([this, name = std::move(name), time = std::move(time), done = std::move(done)] {
		if (s_ == nullptr) return;
		namespace fs = std::filesystem;
		Branch b;
		b.name = name;
		b.time = time;
		b.frame = ce_session_frame(s_);
		b.text = movieTextOnMachine();
		b.frames = ce_session_movie_length(s_);
		/* the picture shown now, which a state load will not draw */
		auto picture = std::make_shared<Picture::Frame>();
		screen.takeIfNewer(~uint64_t{0}, *picture);
		if (picture->width > 0 && picture->height > 0) b.picture = picture;
		std::string why;
		if (const fs::path dir = branchDirOnMachine(); !dir.empty())
		{
			std::error_code ec;
			fs::create_directories(dir, ec);
			const std::string stem = branch_state::nameFor(b.time, b.frame, b.text);
			const fs::path state = dir / (stem + ".state");
			if (ce_session_state_save_file(s_, branch_state::utf8(state).c_str(), nullptr, 0) != 0)
				why = ce_session_last_error(s_);
			else if (!branch_state::writeNote(dir / (stem + ".note"),
						 {machineIdOnMachine(), b.frame, b.picture ? *b.picture : Picture::Frame{}}))
			{
				why = "its note could not be written in " + dir.string();
				fs::remove(state, ec);
			}
			else
				b.state = stem;
		}
		later([done, b, why] { done(b, why); });
	});
}

void SessionViewModel::loadBranch(Branch b, std::function<void(bool, std::string)> done)
{
	submit([this, b = std::move(b), done = std::move(done)] {
		if (s_ == nullptr) return;
		namespace fs = std::filesystem;
		runningM_ = false;
		later([this] { running.set(false); });
		bool fromState = false;
		std::string why;
		const fs::path dir = branchDirOnMachine();
		branch_state::Note note;
		const bool fits = !b.state.empty() && !dir.empty() && branch_state::readNote(dir / (b.state + ".note"), note) &&
						  note.frame == b.frame && note.machine == machineIdOnMachine();
		const int64_t was = ce_session_frame(s_);
		replaceMovieOnMachine(b.text);
		if (fits)
		{
			const int32_t loaded = ce_session_state_load_file(s_, branch_state::utf8(dir / (b.state + ".state")).c_str(), nullptr, 0, nullptr);
			if (loaded == 0 && ce_session_movie_at(s_, b.frame) == 0)
			{
				fromState = true;
				frameM_ = b.frame;
				const Picture::Frame &shown = b.picture ? *b.picture : note.picture;
				if (shown.width > 0 && shown.height > 0) screen.publish(shown.pixels.data(), shown.width, shown.height, shown.aspect);
				for (const Hooks &h : hooksM_)
					if (h.frame) h.frame(s_);
			}
			else
			{
				why = ce_session_last_error(s_);
				/* a state the machine took and could not use leaves it on no
				 * frame at all: back to a stored one first */
				if (loaded == 1)
				{
					const int64_t base = ce_session_greenzone_nearest(s_, std::min(was, b.frame));
					if (base >= 0 && ce_session_greenzone_restore(s_, base) == 0) frameM_ = ce_session_frame(s_);
				}
			}
		}
		if (!fromState) seekOnMachine(b.frame);
		later([this, done, fromState, why] {
			inputsReset.emit();
			done(fromState, why);
		});
		report(-1, -1, -1, -1, -1);
	});
}

void SessionViewModel::forgetBranch(Branch b)
{
	submit([this, b = std::move(b)] {
		if (b.state.empty()) return;
		/* another branch may be the same one */
		for (const Branch &other : branchesM_)
			if (other.state == b.state) return;
		const std::filesystem::path dir = branchDirOnMachine();
		if (dir.empty()) return;
		std::error_code ec;
		std::filesystem::remove(dir / (b.state + ".state"), ec);
		std::filesystem::remove(dir / (b.state + ".note"), ec);
	});
}

void SessionViewModel::tidyBranchStatesOnMachine()
{
	namespace fs = std::filesystem;
	const fs::path dir = branchDirOnMachine();
	std::error_code ec;
	if (dir.empty() || !fs::is_directory(dir, ec)) return;
	std::vector<fs::path> gone;
	for (const auto &entry : fs::directory_iterator(dir, ec))
	{
		const fs::path f = entry.path();
		const std::string ext = f.extension().string();
		if (ext != ".state" && ext != ".note" && ext != ".writing") continue;
		const std::string stem = f.stem().string();
		bool kept = false;
		for (const Branch &b : branchesM_) kept = kept || (!b.state.empty() && stem.rfind(b.state, 0) == 0 && ext != ".writing");
		if (!kept) gone.push_back(f);
	}
	for (const fs::path &f : gone) fs::remove(f, ec);
}

std::string SessionViewModel::movieTextOnMachine() const
{
	if (s_ == nullptr) return {};
	uint64_t len = 0;
	const char *t = ce_session_movie_text(s_, &len);
	return std::string(t != nullptr ? t : "", static_cast<size_t>(len));
}

void SessionViewModel::loadMovieOnMachine(ce_session *s, const std::string &text)
{
	ce_movie_log *log = ce_movie_log_new();
	if (!text.empty()) ce_movie_log_parse(log, text.data(), text.size());
	ce_session_movie_load(s, log);
	ce_movie_log_free(log);
}

void SessionViewModel::pinMarkersOnMachine()
{
	if (s_ == nullptr) return;
	ce_session_greenzone_unpin_all(s_);
	for (const Marker &m : markersM_)
		if (m.keepState) ce_session_greenzone_pin(s_, m.frame, 1);
}

void SessionViewModel::setFinders(PackageFinder packages, FolderFinder firmwareFolders, ExtensionFinder byExtension)
{
	byExtension_ = byExtension;
	submit([this, packages = std::move(packages), firmwareFolders = std::move(firmwareFolders)] {
		findPackageM_ = packages;
		firmwareFoldersM_ = firmwareFolders;
	});
}

bool SessionViewModel::firmwareOnMachine(const std::string &package, const std::string &slots, const std::string &settings,
	const std::string &pins, const std::string &game, const std::vector<std::pair<std::string, std::string>> &files,
	Firmware &out, std::string &why)
{
	json::Json cfg, settingsJson, pinsJson;
	std::string error;
	const char *perr = nullptr;
	if (ce_package *pk = ce_package_open(package.c_str(), &perr))
	{
		uint64_t len = 0;
		if (const uint8_t *d = ce_package_entry(pk, "waterbox.config", &len))
			json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, error);
		ce_package_free(pk);
	}
	json::Json decl, settingsDecl;
	decl.kind = settingsDecl.kind = json::Json::Kind::Array;
	if (const json::Json *list = cfg.find("firmware")) decl.items = list->items;
	if (const json::Json *list = cfg.find("settings")) settingsDecl.items = list->items;
	/* what this game needs beyond the package's: only the core can say, and
	 * it is asked now rather than read from the project, so a game opened
	 * with no project is completed the same way. No machine is open while
	 * it is asked. A core that cannot answer needs nothing more here. */
	if (const json::Json *own = cfg.find("gameFirmware"); own != nullptr && own->boolean && !game.empty())
	{
		std::string unanswered;
		const std::string text = askForSuggestion(package, game, withCueTracks(game, files), slots, settings, unanswered);
		json::Json answer;
		if (!text.empty() && json::parse(text, answer, error) && answer.kind == json::Json::Kind::Object)
		{
			const json::Json theirs = gameFirmwareOf(answer, cfg.find("firmware"));
			decl.items.insert(decl.items.end(), theirs.items.begin(), theirs.items.end());
			for (json::Json &d : gameSettingsOf(answer, cfg.find("settings"))) settingsDecl.items.push_back(std::move(d));
		}
	}
	if (decl.items.empty()) return true;
	if (!json::parse(settings, settingsJson, error) || settingsJson.kind != json::Json::Kind::Object) settingsJson.kind = json::Json::Kind::Object;
	if (!pins.empty()) json::parse(pins, pinsJson, error);
	const std::vector<FirmwareFile> index = indexFirmware(firmwareFoldersM_ ? firmwareFoldersM_() : std::vector<std::string>{});
	const std::vector<FirmwareNeed> needs = neededFirmware(decl, settingsDecl, slots, settingsJson, pinsJson, index);
	std::string missing;
	json::Json recorded;
	recorded.kind = json::Json::Kind::Array;
	for (const FirmwareNeed &n : needs)
	{
		if (n.path.empty())
		{
			missing += (missing.empty() ? "" : ", ") + (n.display.empty() ? n.id : n.display + " (" + n.id + ")");
			continue;
		}
		std::ifstream in(n.path, std::ios::binary);
		out.ids.push_back(n.id);
		out.data.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		json::Json pin;
		pin.kind = json::Json::Kind::Object;
		pin.members.emplace_back("id", json::Json::string(n.id));
		pin.members.emplace_back("sha1", json::Json::string(n.sha1));
		recorded.items.push_back(std::move(pin));
	}
	out.pins = json::write(recorded);
	if (!missing.empty())
	{
		why = "Firmware missing (put it in the firmware folder): " + missing;
		return false;
	}
	return true;
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
}

void SessionViewModel::openProjectOnMachine(const std::string &path, const std::string &packageGiven, bool keepInputs,
	ce_project *reuse)
{
	const std::string kept = keepInputs && s_ != nullptr ? movieTextOnMachine() : std::string();
	const uint64_t keptRerecords = keepInputs && s_ != nullptr ? ce_session_rerecords(s_) : 0;
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
	/* the files are looked for beside the project (more places, later); a
	 * project not saved yet has them where they were picked */
	if (!path.empty())
	{
		const size_t slash = path.find_last_of("/\\");
		const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
		ce_project_resolve_dir(p, dir.c_str());
	}
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
	std::string package = packageGiven;
	if (package.empty())
	{
		const std::string coreName = ce_project_core_name(p) != nullptr ? ce_project_core_name(p) : "";
		std::string found;
		if (findPackageM_) package = findPackageM_(coreName, pinned, found);
		if (package.empty())
		{
			if (reuse == nullptr) ce_project_free(p);
			fail("No " + coreName + " core is installed: put its .chimeraCore in the cores folder");
			return;
		}
		if (!found.empty()) note = "; " + found;
	}
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
	/* the firmware it needs, by the hashes it pins */
	Firmware fw;
	{
		uint64_t len = 0;
		const char *slots = ce_project_slots_text(p, &len);
		const std::string slotsText(slots != nullptr ? slots : "{}", slots != nullptr ? static_cast<size_t>(len) : 2);
		const char *settings = ce_project_settings_text(p, &len);
		const std::string settingsText(settings != nullptr ? settings : "{}", settings != nullptr ? static_cast<size_t>(len) : 2);
		const char *pins = ce_project_firmware_text(p, &len);
		const std::string pinsText(pins != nullptr ? pins : "", pins != nullptr ? static_cast<size_t>(len) : 0);
		/* the game is the project's first file; every file is mounted
		 * under its name */
		std::string game;
		std::vector<std::pair<std::string, std::string>> files;
		for (int32_t i = 0; i < ce_project_file_count(p); i++)
		{
			const char *name = ce_project_file_name(p, i), *source = ce_project_file_source_path(p, i);
			if (name == nullptr || source == nullptr || *source == '\0') continue;
			if (game.empty()) game = source;
			files.emplace_back(name, source);
		}
		std::string why;
		if (!firmwareOnMachine(package, slotsText, settingsText, pinsText, game, files, fw, why))
		{
			if (reuse == nullptr) ce_project_free(p);
			fail(why);
			return;
		}
	}
	std::vector<const char *> fwIds;
	std::vector<const uint8_t *> fwData;
	std::vector<uint64_t> fwLens;
	for (size_t i = 0; i < fw.ids.size(); i++)
	{
		fwIds.push_back(fw.ids[i].c_str());
		fwData.push_back(fw.data[i].data());
		fwLens.push_back(fw.data[i].size());
	}
	ce_session *s = ce_session_open_project(package.c_str(), p, nullptr, fwIds.data(), fwData.data(), fwLens.data(),
		static_cast<int32_t>(fwIds.size()), flags, &error);
	/* what was mounted is what the project records, as always: by the bytes */
	if (s != nullptr && !fw.ids.empty())
	{
		const char *ferr = nullptr;
		ce_project_set_firmware_text(p, fw.pins.c_str(), &ferr);
	}
	if (s == nullptr)
	{
		ce_project_free(p);
		fail(std::string("Could not open the project's machine: ") + (error != nullptr ? error : "no reason given"));
		return;
	}

	/* the movie: the project's, given to the machine, which keeps it from
	 * now on and writes every line of it - or, started again, the one it had */
	if (keepInputs)
		loadMovieOnMachine(s, kept);
	else
	{
		uint64_t len = 0;
		const char *text = ce_project_log_text(p, &len);
		loadMovieOnMachine(s, std::string(text != nullptr ? text : "", static_cast<size_t>(len)));
	}
	/* the count goes on from the project's, or from where a restart was */
	ce_session_set_rerecords(s, keepInputs ? keptRerecords : ce_project_rerecords(p));
	/* the Greenzone box's choice, from TAStudio's part of the project */
	if (!keepInputs)
	{
		int64_t period = 1;
		uint64_t tlen = 0;
		json::Json tastudio;
		std::string terr;
		if (const char *t = ce_project_tastudio_text(p, &tlen);
			t != nullptr && tlen > 0 && json::parse(std::string_view(t, static_cast<size_t>(tlen)), tastudio, terr))
			if (const json::Json *o = tastudio.find("o"))
				if (const json::Json *g = o->find("GreenzonePeriod")) period = std::max<int64_t>(0, static_cast<int64_t>(g->number()));
		periodM_.store(period);
		later([this, period] { greenzonePeriod.set(period); });
	}

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
		/* the branches: their movies as text, as the project keeps them */
		std::vector<Branch> branches;
		for (int32_t i = 0; i < ce_project_branch_count(p); i++)
		{
			Branch b;
			b.name = ce_project_branch_name(p, i) != nullptr ? ce_project_branch_name(p, i) : "";
			b.time = ce_project_branch_time(p, i) != nullptr ? ce_project_branch_time(p, i) : "";
			b.frame = ce_project_branch_frame(p, i);
			uint64_t blen = 0;
			const char *btext = ce_project_branch_log_text(p, i, &blen);
			b.text.assign(btext != nullptr ? btext : "", static_cast<size_t>(blen));
			ce_movie_log *bl = ce_movie_log_new();
			if (!b.text.empty()) ce_movie_log_parse(bl, b.text.data(), b.text.size());
			b.frames = ce_movie_log_count(bl);
			ce_movie_log_free(bl);
			for (int32_t k = 0; k < ce_project_branch_marker_count(p, i); k++)
			{
				const char *t = ce_project_branch_marker_text(p, i, k);
				b.markers.push_back({ce_project_branch_marker_frame(p, i, k), t != nullptr ? t : "",
					ce_project_branch_marker_keep_state(p, i, k) != 0});
			}
			b.state = branch_state::nameFor(b.time, b.frame, b.text);
			branches.push_back(std::move(b));
		}
		branchesM_ = branches;
		later([this, branches] { branchesLoaded.emit(branches); });
	}
	if (projectM_ != nullptr && projectM_ != p) ce_project_free(projectM_);
	projectM_ = p;
	projectPathM_ = path;
	package_ = package;
	game_.clear();
	tidyBranchStatesOnMachine();
	const std::string title = ce_project_title(p) != nullptr ? ce_project_title(p) : "";
	adoptOnMachine(s, package, (title.empty() ? path : title) + note, true);
	{
		const char *first = ce_project_file_count(p) > 0 ? ce_project_file_name(p, 0) : nullptr;
		std::string stem = first != nullptr ? std::filesystem::path(first).stem().string() : title;
		later([this, stem] { gameName.set(stem); });
	}
	startProjectSessionOnMachine(!keepInputs);
	ProjectAnswers answers;
	answers.coreName = ce_project_core_name(p) != nullptr ? ce_project_core_name(p) : "";
	answers.package = package;
	{
		uint64_t len = 0;
		const char *settings = ce_project_settings_text(p, &len);
		answers.settings = settings != nullptr ? std::string(settings, static_cast<size_t>(len)) : "{}";
	}
	for (int32_t i = 0; i < ce_project_file_count(p); i++)
	{
		const char *slot = ce_project_file_slot(p, i), *source = ce_project_file_source_path(p, i);
		if (source != nullptr && *source != '\0') answers.files.emplace_back(slot != nullptr ? slot : "", source);
	}
	later([this, path, title, answers] {
		projectPath.set(path);
		projectTitle.set(title);
		hasProject.set(true);
		lastAnswers_ = answers;
		if (!path.empty()) addRecent(path);
	});
}

void SessionViewModel::catchPollOnMachine()
{
	catchDue_ = Clock::now() + std::chrono::milliseconds(16);
	if (!padsM_) return;
	std::vector<std::string> held;
	padsM_->poll(held);
	for (const std::string &name : held)
		if (std::find(catchHeldM_.begin(), catchHeldM_.end(), name) == catchHeldM_.end())
		{
			catchingM_ = false;
			later([this, name] {
				caughtPad.set(name);
				catchingPad.set(false);
			});
			return;
		}
	/* let go since: pressed again, it counts */
	catchHeldM_ = held;
}

void SessionViewModel::openMadeProjectOnMachine(ce_project *p, const std::string &package)
{
	/* the project open before is done with: this one takes its place */
	freeSessionOnMachine();
	freeProjectOnMachine();
	openProjectOnMachine("", package, false, p);
}

void SessionViewModel::addRecent(const std::string &path)
{
	/* newest first, each once, ten at most */
	std::string list = path, rest = recentProjects.get();
	int kept = 1;
	size_t at = 0;
	while (at <= rest.size() && kept < 10)
	{
		const size_t end = rest.find('\n', at);
		const std::string line = rest.substr(at, end == std::string::npos ? std::string::npos : end - at);
		if (!line.empty() && line != path)
		{
			list += "\n" + line;
			kept++;
		}
		if (end == std::string::npos) break;
		at = end + 1;
	}
	recentProjects.set(list);
}

void SessionViewModel::newProjectOnMachine(const std::string &path, const std::string &titleGiven)
{
	if (s_ == nullptr || game_.empty()) return;
	auto fail = [this](const std::string &why) { later([this, why] { status.set("Not made: " + why); }); };
	json::Json cfg, fileSlots;
	std::string jerr, sha;
	const char *perr = nullptr;
	if (ce_package *pk = ce_package_open(package_.c_str(), &perr))
	{
		uint64_t len = 0;
		if (const uint8_t *d = ce_package_entry(pk, "waterbox.config", &len))
			json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, jerr);
		if (const uint8_t *d = ce_package_entry(pk, "file_slots.json", &len))
			json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), fileSlots, jerr);
		sha = ce_package_sha1(pk) != nullptr ? ce_package_sha1(pk) : "";
		ce_package_free(pk);
	}
	ce_project *p = ce_project_new();
	const json::Json *coreName = cfg.find("coreName"), *version = cfg.find("version");
	ce_project_set_core(p, coreName != nullptr ? coreName->text.c_str() : "", version != nullptr ? version->text.c_str() : "", sha.c_str());
	/* the machine, as the New Project window stamps it */
	if (const char *system = ce_session_system_id(s_); system != nullptr && *system != '\0') ce_project_header_set(p, "Platform", system);
	/* the game, in the package's first slot, by its own name */
	const std::string slotId = gameSlotM_.empty() ? std::string("rom") : gameSlotM_;
	(void)fileSlots;
	const size_t slash = game_.find_last_of("/\\");
	const std::string base = slash == std::string::npos ? game_ : game_.substr(slash + 1);
	const char *error = nullptr;
	if (ce_project_file_add(p, base.c_str(), slotId.c_str(), game_.c_str(), &error) != 0)
	{
		ce_project_free(p);
		fail(error != nullptr ? error : "the game's file could not be added");
		return;
	}
	/* every setting that applies, at its value: the project is the settings */
	{
		json::Json overrides, recorded;
		recorded.kind = json::Json::Kind::Object;
		if (!json::parse(settingsM_.empty() ? "{}" : settingsM_, overrides, jerr)) overrides.kind = json::Json::Kind::Object;
		const json::Json *decl = cfg.find("settings");
		json::Json slotMap;
		slotMap.kind = json::Json::Kind::Object;
		json::Json files;
		files.kind = json::Json::Kind::Array;
		files.items.push_back(json::Json::string(base));
		slotMap.members.emplace_back(slotId, files);
		if (decl != nullptr)
		{
			const std::vector<json::Json> &items = decl->items;
			std::string machineSetting;
			if (const json::Json *m = cfg.find("machineSetting")) machineSetting = m->text;
			for (size_t i : SettingsViewModel::applicable(items, json::write(slotMap), overrides, machineSetting))
			{
				const json::Json *name = items[i].find("name");
				if (name == nullptr) continue;
				const json::Json *v = overrides.find(name->text);
				if (v == nullptr) v = items[i].find("default");
				if (v != nullptr) recorded.members.emplace_back(name->text, *v);
			}
		}
		ce_project_set_settings_text(p, json::write(recorded).c_str(), &error);
	}
	if (!firmwarePinsM_.empty() && firmwarePinsM_ != "[]") ce_project_set_firmware_text(p, firmwarePinsM_.c_str(), &error);
	const std::string title = !titleGiven.empty() ? titleGiven : base.substr(0, base.find_last_of('.'));
	ce_project_set_title(p, title.c_str());
	/* it becomes the open project; saving writes its movie, markers and branches */
	if (projectM_ != nullptr) ce_project_free(projectM_);
	projectM_ = p;
	projectPathM_ = path;
	game_.clear();
	startProjectSessionOnMachine(false);
	saveProjectOnMachine(path);
	later([this, path, title] {
		projectPath.set(path);
		projectTitle.set(title);
		hasProject.set(true);
		addRecent(path);
	});
}

void SessionViewModel::saveProjectOnMachine(const std::string &to)
{
	if (projectM_ == nullptr || s_ == nullptr) return;
	const std::string path = to.empty() ? projectPathM_ : to;
	if (path.empty())
	{
		later([this] { status.set("Not saved: this project has no file yet; say where it goes"); });
		return;
	}
	/* the movie as the machine keeps it: the engine wrote every line */
	const int64_t n = ce_session_movie_length(s_);
	const std::string log = movieTextOnMachine();
	ce_project_set_log_text(projectM_, log.data(), log.size());
	ce_project_set_rerecords(projectM_, ce_session_rerecords(s_));
	/* the Greenzone box's choice, into TAStudio's part, whatever else is there */
	{
		uint64_t tlen = 0;
		json::Json tastudio;
		std::string terr;
		const char *t = ce_project_tastudio_text(projectM_, &tlen);
		if (t == nullptr || tlen == 0 || !json::parse(std::string_view(t, static_cast<size_t>(tlen)), tastudio, terr) ||
			tastudio.kind != json::Json::Kind::Object)
		{
			tastudio = json::Json();
			tastudio.kind = json::Json::Kind::Object;
		}
		json::Json *o = nullptr;
		for (auto &[k, v] : tastudio.members)
			if (k == "o") o = &v;
		if (o == nullptr || o->kind != json::Json::Kind::Object)
		{
			if (o == nullptr)
			{
				tastudio.members.emplace_back("o", json::Json());
				o = &tastudio.members.back().second;
			}
			*o = json::Json();
			o->kind = json::Json::Kind::Object;
			/* the type Chimera's TAStudio reads it as, first, as it writes it:
			 * without it Chimera says the movie's client settings would not load */
			o->members.emplace_back("$type", json::Json::string("Chimera.Client.GUI.TAStudio+MovieClientSettings, Chimera.Client.GUI"));
		}
		bool set = false;
		for (auto &[k, v] : o->members)
			if (k == "GreenzonePeriod")
			{
				v = json::Json::integer(periodM_.load());
				set = true;
			}
		if (!set) o->members.emplace_back("GreenzonePeriod", json::Json::integer(periodM_.load()));
		const char *err = nullptr;
		ce_project_set_tastudio_text(projectM_, json::write(tastudio).c_str(), &err);
	}
	for (int32_t i = ce_project_marker_count(projectM_) - 1; i >= 0; i--) ce_project_marker_remove(projectM_, i);
	for (const Marker &m : markersM_) ce_project_marker_add(projectM_, m.frame, m.text.c_str(), m.keepState ? 1 : 0);
	for (int32_t i = ce_project_branch_count(projectM_) - 1; i >= 0; i--) ce_project_branch_remove(projectM_, i);
	for (const Branch &b : branchesM_)
	{
		ce_project_branch_add(projectM_, b.name.c_str(), b.frame, b.time.c_str(), b.text.data(), b.text.size());
		const int32_t at = ce_project_branch_count(projectM_) - 1;
		for (const Marker &m : b.markers) ce_project_branch_marker_add(projectM_, at, m.frame, m.text.c_str(), m.keepState ? 1 : 0);
	}
	const char *error = nullptr;
	if (ce_project_save(projectM_, path.c_str(), &error) != 0)
	{
		const std::string why = error != nullptr ? error : "no reason given";
		later([this, path, why] { status.set("Could not save " + path + ": " + why); });
		return;
	}
	const bool first = projectPathM_ != path;
	projectPathM_ = path;
	keepHistoryOnMachine();
	later([this, path, n, first] {
		projectPath.set(path);
		if (first) addRecent(path);
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
	packageSha1M_.clear();
	diedM_ = false;
	inputs_.bind(s);
	inputs_.clearLags();
	frameM_ = ce_session_frame(s);
	greenEndM_ = 0;
	/* a history of the machine's states, so a seek replays from the nearest
	 * one rather than from power-on (a setting, later) */
	ce_session_greenzone_enable(s, uint64_t{1} << 30);
	ce_session_greenzone_capture_period(s, periodM_.load());
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

	std::string keybinds, config;
	const char *perr = nullptr;
	if (ce_package *p = ce_package_open(package.c_str(), &perr))
	{
		uint64_t len = 0;
		if (const uint8_t *d = ce_package_entry(p, "default_keybinds.json", &len))
			keybinds.assign(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
		if (const uint8_t *d = ce_package_entry(p, "waterbox.config", &len))
			config.assign(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
		ce_package_free(p);
	}
	/* the analog controls: the engine's names, headers and rest values, the
	 * ranges as the package declares them (its own input, or a machine's) */
	std::vector<Axis> axes;
	{
		json::Json cfg;
		std::string jerr;
		const bool read = json::parse(config, cfg, jerr);
		auto declared = [&](const std::string &name) -> const json::Json * {
			if (!read) return nullptr;
			auto look = [&](const json::Json *input) -> const json::Json * {
				if (input == nullptr) return nullptr;
				if (const json::Json *list = input->find("axes"))
					for (const json::Json &a : list->items)
						if (const json::Json *n = a.find("name"); n != nullptr && n->text == name) return &a;
				return nullptr;
			};
			if (const json::Json *a = look(cfg.find("input"))) return a;
			if (const json::Json *machines = cfg.find("machines"))
				for (const json::Json &m : machines->items)
					if (const json::Json *a = look(m.find("input"))) return a;
			return nullptr;
		};
		for (int64_t i = 0; i < ce_session_axis_count(s); i++)
		{
			Axis a;
			const char *n = ce_session_axis_name(s, i);
			a.name = n != nullptr ? n : "";
			const char *h = ce_session_axis_header_of(s, a.name.c_str());
			a.header = h != nullptr ? h : a.name;
			a.neutral = ce_session_axis_neutral(s, i);
			if (const json::Json *d = declared(a.name))
			{
				if (const json::Json *v = d->find("min")) a.min = static_cast<int64_t>(v->number());
				if (const json::Json *v = d->find("max")) a.max = static_cast<int64_t>(v->number());
			}
			axes.push_back(a);
		}
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

	const int64_t frames = inputs_.size();
	later([this, names, axes, mnemonics, keybinds, coreText, systemText, rate, label, inputsReplaced, frames] {
		buttonNames_ = names;
		axes_ = axes;
		mnemonics_ = mnemonics;
		std::string why;
		const bool bound = keybinds.empty() || bindings_.parse(keybinds, buttonNames_, why);
		defaultBindings_ = bindings_.texts();
		defaultBindings_.resize(buttonNames_.size());
		buttonCount.set(static_cast<int64_t>(names.size()));
		coreName.set(coreText);
		systemName.set(systemText);
		frame.set(0);
		greenEnd.set(0);
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
		hasProject.set(false);
		status.set("Closed");
	});
}

void SessionViewModel::advanceOnMachine()
{
	const int64_t f = ce_session_frame(s_);
	const int64_t size = ce_session_movie_length(s_);
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

int32_t SessionViewModel::stepOnMachine(bool live, bool render, bool grow)
{
	movesM_++;
	const int64_t f = ce_session_frame(s_);
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
	/* What is held goes into the movie before the frame plays it: past the
	 * end the movie grows, and over a frame it has, a press it lacks is an
	 * edit of that frame (the engine drops the states after it). The engine
	 * plays the movie; the window never hands the machine input of its own. */
	bool changed = false;
	const size_t axisCount = static_cast<size_t>(ce_session_axis_count(s_));
	axesBuf_.resize(axisCount);
	uint64_t logged = 0;
	const int32_t had = ce_session_movie_get(s_, f, &logged, axisCount ? axesBuf_.data() : nullptr);
	if (had == 1 && !grow) return 0;
	if (had == 1)
		ce_session_movie_set(s_, f, held, nullptr);
	else if ((logged | held) != logged)
	{
		ce_session_movie_set(s_, f, logged | held, axisCount ? axesBuf_.data() : nullptr);
		greenEndM_ = std::min(greenEndM_, f);
		changed = true;
	}

	const int32_t lag = ce_session_movie_advance(s_, 0, nullptr, render ? 1 : 0);
	if (lag < 0)
	{
		const char *death = ce_session_guest_death(s_);
		/* the window took the frame out just as it was to be played: the
		 * machine is well, and the edit's own turn puts it right */
		if ((death == nullptr || *death == '\0') && ce_session_movie_length(s_) <= f)
		{
			frameM_ = ce_session_frame(s_);
			return 0;
		}
		runningM_ = false;
		diedM_ = true;
		const std::string why = death != nullptr && *death != '\0' ? std::string("The machine stopped: ") + death
																	 : std::string("The movie could not be played: ") + ce_session_last_error(s_);
		later([this, why] { running.set(false); status.set(why); });
		return lag;
	}
	frameM_ = ce_session_frame(s_);
	greenEndM_ = std::max(greenEndM_, frameM_);
	for (const Hooks &h : hooksM_)
		if (h.frame) h.frame(s_);
	if (inputs_.setLag(f, lag > 0)) changed = true;

	if (live && soundM_)
	{
		int32_t pairs = 0;
		const int16_t *samples = ce_session_audio(s_, &pairs);
		soundM_->play(samples, pairs, soundOnM_ ? static_cast<int32_t>(volumeM_.load()) : 0);
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

void SessionViewModel::seekOnMachine(int64_t target, bool grow)
{
	if (s_ == nullptr || target < 0) return;
	movesM_++;
	const auto start = Clock::now();
	const int64_t from = ce_session_frame(s_);
	/* a frame past the movie's end: the movie grows to it, with empty frames,
	 * or the machine stops at the end. The length is read here, at the last
	 * moment: the window edits the movie while this thread runs. */
	const int64_t length = ce_session_movie_length(s_);
	if (target > length)
	{
		if (grow) ce_session_movie_resize(s_, target);
		else target = length;
	}
	/* The engine seeks: back to its nearest stored state and forward by the
	 * movie. It draws nothing on the way, so the seek stops a frame short
	 * and the last frame is run with its picture. What it ran, roughly: from
	 * the stored state it went back to, or from where it was. */
	const int64_t want = target > 0 ? target - 1 : 0;
	/* an edit the engine has not honoured yet (greenEnd fell to it) sends
	 * the machine back before it */
	const int64_t stored = ce_session_greenzone_nearest(s_, greenEndM_ < from ? std::min(want, greenEndM_) : want);
	const int64_t ran = (stored >= 0 && (stored > from || from > want) ? want - stored : want - from) + (target > 0 ? 1 : 0);
	if (ce_session_seek(s_, want) != 0)
	{
		/* the window edited the movie while the machine went through it
		 * (frames taken out, and maybe put back by an undo since): that
		 * edit's own turn comes next and puts the machine right */
		if (!grow && (ce_session_movie_length(s_) < length || editsSent_.load() != editRunningM_))
		{
			frameM_ = ce_session_frame(s_);
			report(-1, -1, -1, -1, -1);
			return;
		}
		const std::string why = std::string("Could not go to frame ") + std::to_string(target) + ": " + ce_session_last_error(s_);
		later([this, why] { status.set(why); });
		frameM_ = ce_session_frame(s_);
		report(-1, -1, -1, -1, -1);
		return;
	}
	frameM_ = ce_session_frame(s_);
	if (target > 0) stepOnMachine(false, true, grow);
	const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	later([this, ms, ran] {
		lastSeekMs.set(ms);
		lastSeekFrames.set(ran);
	});
	report(-1, -1, -1, -1, -1);
}

void SessionViewModel::dropStatesAfterOnMachine(int64_t frame)
{
	if (s_ == nullptr) return;
	ce_session_greenzone_invalidate(s_, frame);
	greenEndM_ = std::min(greenEndM_, frame);
	report(-1, -1, -1, -1, -1);
}

void SessionViewModel::edited(int64_t first)
{
	const uint64_t sent = ++editsSent_;
	submit([this, first, sent] {
		if (s_ == nullptr) return;
		editRunningM_ = sent;
		greenEndM_ = std::min(greenEndM_, first);
		/* moved since the last edit moved it: that edit's frame is forgotten */
		if (movesM_ != restoredMovesM_ || frameM_ != restoredAtM_) restoreFrameM_ = -1;
		const int64_t length = ce_session_movie_length(s_);
		/* an edit that gave a movie made shorter its frames again (an undo)
		 * lets the machine go on to where the first edit found it */
		const bool shortOfIt = autoRestoreM_.load() && restoreFrameM_ > frameM_ && length > frameM_;
		if (frameM_ > first || shortOfIt)
		{
			/* The machine is past the edit. The engine honours it before the
			 * machine next moves; here the window says where: with autoRestore
			 * back on the frame it was on at the first edit, with the new
			 * inputs, else at the edit. Going back adds no frames to the
			 * movie: one made shorter is followed to its end. */
			if (restoreFrameM_ < 0) restoreFrameM_ = frameM_;
			seekOnMachine(autoRestoreM_.load() ? restoreFrameM_ : first, false);
			restoredMovesM_ = movesM_;
			restoredAtM_ = frameM_;
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
		pendingRerecords_ = s_ != nullptr ? static_cast<int64_t>(ce_session_rerecords(s_)) : 0;
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
	int64_t f, cf, cl, af, al, green, states, rr;
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
		rr = pendingRerecords_;
		pendingFps_ = -1;
		changedFirst_ = changedLast_ = appendedFirst_ = appendedLast_ = -1;
	}
	frame.set(f);
	if (measured >= 0) fps.set(measured);
	soundQueued.set(queued);
	soundDropped.set(dropped);
	greenEnd.set(green);
	greenzoneStates.set(states);
	rerecords.set(rr);
	updateOverlay(f, cf >= 0 ? cf : af);
	if (cf >= 0) inputsChanged.emit(cf, cl);
	if (af >= 0) inputsAppended.emit(af, al);
	screenChanged.emit();
}

void SessionViewModel::updateOverlay(int64_t f, int64_t changedFrom)
{
	if (showFps.get())
		fpsText.set(std::to_string(static_cast<int64_t>(std::llround(fps.get()))) + " fps" +
			(rewindHeld_ ? " <<" : fastHeld_ ? " >>" : ""));
	lagFrame.set(f > 0 && inputs_.lagAt(f - 1));
	const int64_t size = inputs_.size();
	if (showFrameCounter.get())
	{
		/* the last frame with input: zero for a movie with none */
		if (changedFrom >= 0 || lastInputFor_ != size)
		{
			lastInput_ = 0;
			std::vector<int32_t> axes(inputs_.axisCount()), neutral(inputs_.axisCount());
			if (!neutral.empty()) inputs_.neutralAxes(neutral.data());
			for (int64_t at = size - 1; at >= 0; at--)
			{
				bool pressed = inputs_.at(at) != 0;
				if (!pressed && !axes.empty())
				{
					inputs_.axesAt(at, axes.data());
					pressed = axes != neutral;
				}
				if (pressed)
				{
					lastInput_ = at;
					break;
				}
			}
			lastInputFor_ = size;
		}
		frameCounterText.set(std::to_string(f) + "/" + std::to_string(lastInput_) + (size > 0 && f > lastInput_ ? " (Finished)" : ""));
	}
	if (showLagCounter.get())
	{
		/* counted on from where it was, unless something before changed */
		if (f < lagsTo_ || (changedFrom >= 0 && changedFrom < lagsTo_))
		{
			lagsTo_ = 0;
			lags_ = 0;
		}
		for (; lagsTo_ < f; lagsTo_++) lags_ += inputs_.lagAt(lagsTo_) ? 1 : 0;
		lagCount.set(lags_);
	}
	if (showInput.get()) inputText.set(inputDisplay(f - 1));
}

std::string SessionViewModel::inputDisplay(int64_t frame) const
{
	if (frame < 0 || frame >= inputs_.size()) return {};
	std::vector<int32_t> values(inputs_.axisCount());
	if (!values.empty()) inputs_.axesAt(frame, values.data());
	return inputDisplay(buttonNames_, mnemonics_, axes_, inputs_.at(frame), values);
}

std::string SessionViewModel::inputDisplay(const std::vector<std::string> &buttons, const std::string &mnemonics,
	const std::vector<Axis> &axes, uint64_t mask, const std::vector<int32_t> &values)
{
	/* a control's player: the number after "P" in its name, 0 for the console */
	auto player = [](const std::string &name) {
		if (name.size() < 3 || name[0] != 'P' || !std::isdigit(static_cast<unsigned char>(name[1]))) return 0;
		size_t at = 1;
		int n = 0;
		while (at < name.size() && std::isdigit(static_cast<unsigned char>(name[at]))) n = n * 10 + (name[at++] - '0');
		return at < name.size() && name[at] == ' ' ? n : 0;
	};
	int most = 0;
	for (const std::string &n : buttons) most = std::max(most, player(n));
	for (const Axis &a : axes) most = std::max(most, player(a.name));
	std::string out;
	for (int i = 1; i <= most + 1; i++)
	{
		const int p = i <= most ? i : 0;
		for (size_t b = 0; b < buttons.size(); b++)
			if (player(buttons[b]) == p)
			{
				const char mn = b < mnemonics.size() ? mnemonics[b] : (buttons[b].empty() ? '?' : buttons[b].back());
				out += b < 64 && (mask >> b & 1) ? mn : ' ';
			}
		for (size_t x = 0; x < axes.size() && x < values.size(); x++)
			if (player(axes[x].name) == p)
			{
				if (values[x] == axes[x].neutral) out += "      ";
				else
				{
					std::string v = std::to_string(values[x]);
					if (v.size() < 5) v.insert(0, 5 - v.size(), ' ');
					out += v + ",";
				}
			}
	}
	return out;
}

} // namespace chimera::vm
