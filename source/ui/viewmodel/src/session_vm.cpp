#include "chimera/vm/session_vm.hpp"

#include <algorithm>
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
	expose("recentProjects", "projects opened, newest first, a line each", recentProjects, true);
	expose("chooseFor", "a game waiting for its core to be chosen", chooseFor);
	expose("choices", "the cores the waiting game could be opened with, a line each", choices);
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
			if (projectPath.get().empty())
			{
				error = "no project is open";
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
			if (!projectPath.get().empty())
			{
				error = "a project is open: Save Project As keeps it";
				return {};
			}
			submit([this, path, title] { newProjectOnMachine(path, title); });
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

void SessionViewModel::openOnMachine(const std::string &package, const std::string &game, bool keepMovie)
{
	const std::string movie = keepMovie && s_ != nullptr ? movieTextOnMachine() : std::string();
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
		if (!firmwareOnMachine(package, json::write(slotMap), settingsM_.empty() ? "{}" : settingsM_, "", fw, why))
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
	later([this] { projectPath.set(""); projectTitle.set(""); });
	/* the movie: the one it had when started again, else an empty one */
	loadMovieOnMachine(s, movie);
	adoptOnMachine(s, package, game, true);
	package_ = package;
	game_ = game;
}

void SessionViewModel::freeSessionOnMachine()
{
	if (s_ == nullptr) return;
	for (const Hooks &h : hooksM_)
		if (h.closing) h.closing(s_);
	inputs_.bind(nullptr);
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
		seekOnMachine(target);
		later([this] { inputsReset.emit(); });
		report(-1, -1, -1, -1, -1);
	});
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
	const std::string &pins, Firmware &out, std::string &why)
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
	const json::Json *decl = cfg.find("firmware");
	if (decl == nullptr || decl->items.empty()) return true;
	json::Json noSettings;
	const json::Json *settingsDecl = cfg.find("settings");
	if (!json::parse(settings, settingsJson, error) || settingsJson.kind != json::Json::Kind::Object) settingsJson.kind = json::Json::Kind::Object;
	if (!pins.empty()) json::parse(pins, pinsJson, error);
	const std::vector<FirmwareFile> index = indexFirmware(firmwareFoldersM_ ? firmwareFoldersM_() : std::vector<std::string>{});
	const std::vector<FirmwareNeed> needs = neededFirmware(*decl, settingsDecl != nullptr ? *settingsDecl : noSettings, slots,
		settingsJson, pinsJson, index);
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
		std::string why;
		if (!firmwareOnMachine(package, slotsText, settingsText, pinsText, fw, why))
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
	const std::string title = ce_project_title(p) != nullptr ? ce_project_title(p) : "";
	adoptOnMachine(s, package, (title.empty() ? path : title) + note, true);
	later([this, path, title] {
		projectPath.set(path);
		projectTitle.set(title);
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
	});
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
	saveProjectOnMachine(path);
	later([this, path, title] {
		projectPath.set(path);
		projectTitle.set(title);
		std::string list = path;
		for (size_t at = 0, n = 1; at < recentProjects.get().size() && n < 10;)
		{
			const size_t end = recentProjects.get().find('\n', at);
			const std::string line = recentProjects.get().substr(at, end == std::string::npos ? std::string::npos : end - at);
			if (!line.empty() && line != path) { list += "\n" + line; n++; }
			if (end == std::string::npos) break;
			at = end + 1;
		}
		recentProjects.set(list);
	});
}

void SessionViewModel::saveProjectOnMachine(const std::string &to)
{
	if (projectM_ == nullptr || s_ == nullptr) return;
	const std::string path = to.empty() ? projectPathM_ : to;
	/* the movie as the machine keeps it: the engine wrote every line */
	const int64_t n = ce_session_movie_length(s_);
	const std::string log = movieTextOnMachine();
	ce_project_set_log_text(projectM_, log.data(), log.size());
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
	inputs_.bind(s);
	inputs_.clearLags();
	frameM_ = ce_session_frame(s);
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

int32_t SessionViewModel::stepOnMachine(bool live, bool render)
{
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
		runningM_ = false;
		const char *death = ce_session_guest_death(s_);
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
	if (s_ == nullptr || target < 0) return;
	const auto start = Clock::now();
	const int64_t from = ce_session_frame(s_);
	/* a frame past the movie's end: the movie grows to it, with empty frames */
	if (target > ce_session_movie_length(s_)) ce_session_movie_resize(s_, target);
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
		const std::string why = std::string("Could not go to frame ") + std::to_string(target) + ": " + ce_session_last_error(s_);
		later([this, why] { status.set(why); });
		frameM_ = ce_session_frame(s_);
		report(-1, -1, -1, -1, -1);
		return;
	}
	frameM_ = ce_session_frame(s_);
	if (target > 0) stepOnMachine(false, true);
	const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	later([this, ms, ran] {
		lastSeekMs.set(ms);
		lastSeekFrames.set(ran);
	});
	report(-1, -1, -1, -1, -1);
}

void SessionViewModel::edited(int64_t first)
{
	submit([this, first] {
		if (s_ == nullptr) return;
		greenEndM_ = std::min(greenEndM_, first);
		if (frameM_ > first)
		{
			/* The machine is past the edit. The engine honours it before the
			 * machine next moves; here the window says where: with autoRestore
			 * back on the frame it was on, with the new inputs, else at the
			 * edit. */
			seekOnMachine(autoRestoreM_.load() ? frameM_ : first);
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
