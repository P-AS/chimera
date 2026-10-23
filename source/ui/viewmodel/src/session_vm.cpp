#include "chimera/vm/session_vm.hpp"

#include "chimera/vm/branch_state.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <future>
#include <iterator>

#include "chimera/engine.h"
#include "chimera/vm/firmware.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/settings_vm.hpp"
#include "chimera/vm/suggestion.hpp"

namespace chimera::vm {

namespace {
/* A zip of the files given, each stored as it is (the savedata export's,
 * for more than one file; a zip any unzipper opens) */
uint32_t crc32Of(const std::vector<uint8_t> &bytes)
{
	static uint32_t table[256];
	static bool made = false;
	if (!made)
	{
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		made = true;
	}
	uint32_t c = 0xFFFFFFFFu;
	for (uint8_t b : bytes) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}
} // namespace

std::vector<uint8_t> storedZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files)
{
	std::vector<uint8_t> out, central;
	auto u16 = [](std::vector<uint8_t> &v, uint32_t x) { v.push_back(x & 0xFF), v.push_back((x >> 8) & 0xFF); };
	auto u32 = [&](std::vector<uint8_t> &v, uint32_t x) { u16(v, x & 0xFFFF), u16(v, x >> 16); };
	for (const auto &[name, bytes] : files)
	{
		const uint32_t crc = crc32Of(bytes), size = static_cast<uint32_t>(bytes.size()), at = static_cast<uint32_t>(out.size());
		u32(out, 0x04034b50), u16(out, 20), u16(out, 0x0800), u16(out, 0), u16(out, 0), u16(out, 0x21);
		u32(out, crc), u32(out, size), u32(out, size), u16(out, static_cast<uint32_t>(name.size())), u16(out, 0);
		out.insert(out.end(), name.begin(), name.end());
		out.insert(out.end(), bytes.begin(), bytes.end());
		u32(central, 0x02014b50), u16(central, 20), u16(central, 20), u16(central, 0x0800), u16(central, 0), u16(central, 0);
		u16(central, 0x21), u32(central, crc), u32(central, size), u32(central, size), u16(central, static_cast<uint32_t>(name.size()));
		u16(central, 0), u16(central, 0), u16(central, 0), u16(central, 0), u32(central, 0), u32(central, at);
		central.insert(central.end(), name.begin(), name.end());
	}
	const uint32_t centralAt = static_cast<uint32_t>(out.size()), centralSize = static_cast<uint32_t>(central.size());
	out.insert(out.end(), central.begin(), central.end());
	u32(out, 0x06054b50), u16(out, 0), u16(out, 0), u16(out, static_cast<uint32_t>(files.size()));
	u16(out, static_cast<uint32_t>(files.size())), u32(out, centralSize), u32(out, centralAt), u16(out, 0);
	return out;
}

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
	expose("showAverageFps", "the average speed since it was turned on or reset is shown on the speed's line", showAverageFps, true);
	expose("averageFpsText", "the average speed as shown: \"avg 59.9 fps\", \"\" until something counted", averageFpsText);
	expose("showRerecords", "the rerecord count is shown over the picture", showRerecords, true);
	expose("showMessages", "messages are shown over the picture a moment", showMessages, true);
	expose("showStatusBar", "the status bar is shown", showStatusBar, true);
	expose("isGameCore", "the core open is a game (its menu is Game, not Emulator)", isGameCore);
	expose("hasGameTimer", "the game open has a timer of its own", hasGameTimer);
	expose("hasSaveData", "the core open writes out the user's save data", hasSaveData);
	expose("hasMovieImport", "the core open imports movies made elsewhere", hasMovieImport);
	expose("showGameTime", "a game's own timer is shown over the picture", showGameTime, true);
	expose("gameTimeText", "the game's timer as shown: \"IGT 01:02.345\"", gameTimeText);
	expose("coreLogPath", "the file the core log goes to, \"\" while it is off", coreLogPath);
	command("coreLogName", "the name Chimera suggests for a core log: the core, \"core log\", the date and time", {},
		[this](const Args &, std::string &) -> Value {
			const std::time_t now = std::time(nullptr);
			char when[32];
			std::strftime(when, sizeof when, "%Y-%m-%d %H.%M.%S", std::localtime(&now));
			const std::string core = isOpen.get() && !coreName.get().empty() ? coreName.get() : "Chimera";
			return core + " core log " + when + ".txt";
		});
	command("startCoreLog", "keeps what the cores write in this file from now on (a session opened from now notes its core)",
		{"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			if (ce_core_log(path.c_str()) != 1)
			{
				const char *why = ce_core_log_error();
				error = "The core log could not be started: " + std::string(why != nullptr ? why : "no reason given");
				return {};
			}
			coreLogPath.set(path);
			return true;
		});
	command("stopCoreLog", "stops keeping the core log; the file stays", {}, [this](const Args &, std::string &) -> Value {
		const std::string was = coreLogPath.get();
		ce_core_log(nullptr);
		coreLogPath.set("");
		if (!was.empty()) status.set("The core log is off. It is in " + was + ": attach that file to your issue.");
		return was;
	});
	command("coreLogSize", "how many bytes the core log has so far", {}, [this](const Args &, std::string &) -> Value {
		std::error_code ec;
		const auto size = std::filesystem::file_size(coreLogPath.get(), ec);
		return ec ? int64_t{0} : static_cast<int64_t>(size);
	});
	overlayWatches_.push_back(showGameTime.changed.connect([this] {
		showGameTimeM_.store(showGameTime.get());
		submit([this] { report(-1, -1, -1, -1, -1); });
	}));
	command("saveDataFiles", "the save files the machine holds now, a line each", {}, [this](const Args &, std::string &error) -> Value {
		std::string names;
		bool open = false;
		onMachineWait([&](ce_session *s) {
			if (s == nullptr || !ce_session_savedata_available(s)) return;
			open = true;
			const int32_t n = ce_session_savedata_count(s);
			for (int32_t i = 0; i < n; i++)
				if (const char *name = ce_session_savedata_name(s, i)) names += (names.empty() ? "" : "\n") + std::string(name);
		});
		if (!open)
		{
			error = "the core open keeps no save data";
			return {};
		}
		return names;
	}, &hasSaveData);
	command("exportSaveData", "writes the save data out, as Chimera's Export Save Data: one file as it is, more in a zip; "
		"says how many files", {"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			std::string why;
			int64_t written = -1;
			onMachineWait([&](ce_session *s) {
				if (s == nullptr || !ce_session_savedata_available(s))
				{
					why = "the core open keeps no save data";
					return;
				}
				const int32_t n = ce_session_savedata_count(s);
				if (n == 0)
				{
					why = "This machine holds no save data yet.";
					return;
				}
				/* each file read in pieces, as the engine streams it */
				auto contents = [&](int32_t i) {
					std::vector<uint8_t> bytes(static_cast<size_t>(std::max<int64_t>(0, ce_session_savedata_size(s, i))));
					int64_t at = 0;
					while (at < static_cast<int64_t>(bytes.size()))
					{
						const int64_t got = ce_session_savedata_read(s, i, at, bytes.data() + at,
							std::min<int64_t>(1 << 20, static_cast<int64_t>(bytes.size()) - at));
						if (got <= 0) break;
						at += got;
					}
					bytes.resize(static_cast<size_t>(at));
					return bytes;
				};
				std::ofstream out(path, std::ios::binary);
				if (n == 1)
				{
					const std::vector<uint8_t> one = contents(0);
					out.write(reinterpret_cast<const char *>(one.data()), static_cast<std::streamsize>(one.size()));
				}
				else
				{
					std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
					for (int32_t i = 0; i < n; i++)
					{
						const char *name = ce_session_savedata_name(s, i);
						files.emplace_back(name != nullptr ? name : "file" + std::to_string(i), contents(i));
					}
					const std::vector<uint8_t> zip = storedZip(files);
					out.write(reinterpret_cast<const char *>(zip.data()), static_cast<std::streamsize>(zip.size()));
				}
				if (!out)
				{
					why = "could not write " + path;
					return;
				}
				written = n;
			});
			if (written < 0)
			{
				error = why;
				return {};
			}
			status.set("Save data written: " + path);
			return written;
		}, &hasSaveData);
	command("resetAverageFps", "the average speed counted afresh from now", {}, [this](const Args &, std::string &) -> Value {
		averageResetM_.store(true);
		averageFpsText.set("");
		return {};
	});
	/* turned on, it counts from then: what ran while nobody looked is not part of it */
	overlayWatches_.push_back(showAverageFps.changed.connect([this] {
		if (!showAverageFps.get()) return;
		averageResetM_.store(true);
		averageFpsText.set("");
	}));
	/* turned on, a counter says at once what it has to say, counted afresh:
	 * off, it heard of no change */
	for (Property<bool> *shown : {&showFps, &showFrameCounter, &showLagCounter, &showInput})
		overlayWatches_.push_back(shown->changed.connect([this] {
			lastInputFor_ = -1;
			lagsTo_ = lags_ = 0;
			updateOverlay(frame.get(), -1);
		}));
	expose("autoheld", "the buttons autohold holds, a space between", autoheld);
	expose("autofired", "the buttons autofire fires, a space between", autofired);
	expose("stickyArmed", "autohold or autofire is armed: a button pressed now is toggled", stickyArmed);
	expose("stickyText", "the sticky buttons as Chimera shows them while arming: \"Held: P1 A Auto-P1 B \"", stickyText);
	expose("autofireOn", "frames autofire presses a button for", autofireOn, true);
	expose("autofireOff", "frames autofire lets it go for", autofireOff, true);
	expose("autofireSkipsLag", "lag frames do not count in autofire's pattern", autofireSkipsLag, true);
	stickyWatches_.push_back(autofireOn.changed.connect([this] { fireOnM_ = std::max<int64_t>(1, autofireOn.get()); }));
	stickyWatches_.push_back(autofireOff.changed.connect([this] { fireOffM_ = std::max<int64_t>(1, autofireOff.get()); }));
	stickyWatches_.push_back(autofireSkipsLag.changed.connect([this] { fireSkipsLagM_ = autofireSkipsLag.get(); }));
	command("clearAutohold", "no button held or fired by autohold and autofire any longer", {}, [this](const Args &, std::string &) -> Value {
		autoholdMask_ = 0;
		autofireMask_ = 0;
		stickyChanged();
		status.set("Autohold keys cleared");
		return {};
	});
	command("toggleAutohold", "a button held by autohold, or no longer", {"button"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		for (size_t b = 0; b < buttonNames_.size() && b < 64; b++)
			if (buttonNames_[b] == name)
			{
				autoholdMask_ ^= uint64_t{1} << b;
				stickyChanged();
				return {};
			}
		error = "no button " + name;
		return {};
	});
	command("toggleAutofire", "a button fired by autofire, or no longer", {"button"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		for (size_t b = 0; b < buttonNames_.size() && b < 64; b++)
			if (buttonNames_[b] == name)
			{
				const uint64_t bit = uint64_t{1} << b;
				autofireMask_ ^= bit;
				submit([this, b] { fireAtM_[b] = 0; });
				stickyChanged();
				return {};
			}
		error = "no button " + name;
		return {};
	});
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
	expose("backupFolder", "where a project's backups go", backupFolder, true);
	command("saveBackup", "writes the project as it stands to the backup folder, named for it and the date and time, "
		"as Chimera's Save Project Backup does; the project's own file is left as it is", {},
		[this](const Args &, std::string &error) -> Value {
			if (projectPath.get().empty())
			{
				error = hasProject.get() ? "this project has not been saved yet" : "no project is open";
				return {};
			}
			if (backupFolder.get().empty())
			{
				error = "no backup folder is set";
				return {};
			}
			const std::string path = backupPathFor(projectPath.get());
			submit([this, path] { saveProjectOnMachine(path, true); });
			return path;
		}, &hasProject);
	command("clearGreenzone", "every stored state but the first frame's thrown away, and the machine back to frame 0, "
		"as Chimera's Clear Greenzone", {}, [this](const Args &, std::string &) -> Value {
			submit([this] {
				if (s_ == nullptr) return;
				ce_session_greenzone_invalidate(s_, 0);
				greenEndM_ = 0;
				seekOnMachine(0, false);
				report(-1, -1, -1, -1, -1);
			});
			return {};
		}, &isOpen);
	command("checkStateHistory", "Chimera's state history check: says in status whether every state the history holds "
		"puts the machine back exactly; the two states of the first that does not are written to the folder given",
		{"keepIn"}, [this](const Args &a, std::string &error) -> Value {
			std::string keepIn;
			if (!a.empty() && !arg(a, 0, keepIn, error)) return {};
			status.set("Checking the state history...");
			submit([this, keepIn] { checkHistoryOnMachine(keepIn); });
			return {};
		}, &isOpen);
	expose("recentProjects", "projects opened, newest first, a line each", recentProjects, true);
	expose("chooseFor", "a game waiting for its core to be chosen", chooseFor);
	expose("choices", "the cores the waiting game could be opened with, a line each", choices);
	picture("screen", "the machine's picture", screen, screenChanged);
	expose("unsaved", "the open project was changed since it was opened or last saved", unsaved);
	expose("pauseAtEnd", "pause on the frame the movie runs out (TAStudio's \"automatically pause at end of movie\")",
		pauseAtEnd, true);
	expose("rewindStep", "how many frames a step of rewind goes back", rewindStep, true);
	expose("rewindStepFast", "how many frames a step of rewind goes back while fast forwarding", rewindStepFast, true);
	expose("autosaveSeconds", "save a project with changes by itself every so many seconds; 0 never", autosaveSeconds, true);
	expose("autosaveAsBackup", "the autosave writes a backup (no greenzone) instead of the project's own file",
		autosaveAsBackup, true);
	expose("backupOnSave", "every save by hand writes a backup as well", backupOnSave, true);
	pauseWatch_ = pauseAtEnd.changed.connect([this] { pauseAtEndM_.store(pauseAtEnd.get()); });
	rewindWatch_ = rewindStep.changed.connect([this] {
		if (rewindStep.get() < 1) rewindStep.set(1);
		rewindStepM_.store(rewindStep.get());
	});
	rewindFastWatch_ = rewindStepFast.changed.connect([this] {
		if (rewindStepFast.get() < 1) rewindStepFast.set(1);
		rewindStepFastM_.store(rewindStepFast.get());
	});
	autosaveWatch_ = autosaveSeconds.changed.connect([this] {
		if (autosaveSeconds.get() < 0) autosaveSeconds.set(0);
		const int64_t ms = autosaveSeconds.get() * 1000;
		autosaveMsM_.store(ms);
		/* counted again from now */
		submit([this] { autosaveDue_ = {}; });
	});
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
			saveTo(path);
			/* "When manually saving, also save .backup" */
			if (backupOnSave.get() && !backupFolder.get().empty())
			{
				const std::string backup = backupPathFor(path.empty() ? projectPath.get() : path);
				submit([this, backup] { saveProjectOnMachine(backup, true); });
			}
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
			passOnMachine(1, true, false);
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
	command("seekToRestore", "goes back to the frame the first of a run of edits found the machine on (Chimera's green arrow)",
		{}, [this](const Args &, std::string &) -> Value {
			submit([this] {
				if (s_ == nullptr || restoreFrameM_ < 0) return;
				seekOnMachine(restoreFrameM_, false);
				restoredMovesM_ = movesM_;
				restoredAtM_ = frameM_;
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

void SessionViewModel::armAutohold(bool down)
{
	armed_ = down ? 1 : (armed_ == 1 ? 0 : armed_.load());
	stickyArmed.set(armed_ != 0);
}

void SessionViewModel::armAutofire(bool down)
{
	armed_ = down ? 2 : (armed_ == 2 ? 0 : armed_.load());
	stickyArmed.set(armed_ != 0);
}

void SessionViewModel::toggleSticky(uint64_t pressed)
{
	/* any thread: armed, the buttons just pressed change over */
	const int armed = armed_.load();
	if (armed == 0 || pressed == 0) return;
	if (armed == 1) autoholdMask_ ^= pressed;
	else
	{
		autofireMask_ ^= pressed;
		submit([this, pressed] {
			for (int b = 0; b < 64; b++)
				if (pressed >> b & 1) fireAtM_[static_cast<size_t>(b)] = 0;
		});
	}
}

void SessionViewModel::stickyChanged()
{
	auto names = [this](uint64_t mask) {
		std::string out;
		for (size_t b = 0; b < buttonNames_.size() && b < 64; b++)
			if (mask >> b & 1) out += (out.empty() ? "" : " ") + buttonNames_[b];
		return out;
	};
	autoheld.set(names(autoholdMask_.load()));
	autofired.set(names(autofireMask_.load()));
	std::string text = "Held: ";
	for (size_t b = 0; b < buttonNames_.size() && b < 64; b++)
		if (autoholdMask_.load() >> b & 1) text += buttonNames_[b] + " ";
	for (size_t b = 0; b < buttonNames_.size() && b < 64; b++)
		if (autofireMask_.load() >> b & 1) text += "Auto-" + buttonNames_[b] + " ";
	stickyText.set(text);
}

void SessionViewModel::key(const std::string &name, bool down)
{
	bool &was = keysDown_[name];
	if (was == down) return;
	was = down;
	/* armed: the buttons this key presses change over (and are pressed as well) */
	if (down && armed_ != 0)
	{
		uint64_t pressed = 0;
		for (int b : bindings_.buttonsOf(name))
			if (b < 64) pressed |= uint64_t{1} << b;
		toggleSticky(pressed);
		stickyChanged();
	}
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
			if (frameM_ > 0) seekOnMachine(std::max<int64_t>(0, frameM_ - (fastM_ ? rewindStepFastM_.load() : rewindStepM_.load())));
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
		bool advance = false, runOn = false, back = false, catchPoll = false, autosaveTick = false;
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
				/* the autosave's clock: every so many seconds while a project is open */
				const int64_t autoMs = autosaveMsM_.load();
				const bool autoOn = autoMs > 0 && projectM_ != nullptr;
				if (autoOn && autosaveDue_ == Clock::time_point{}) autosaveDue_ = Clock::now() + std::chrono::milliseconds(autoMs);
				if (autoOn && Clock::now() >= autosaveDue_)
				{
					autosaveTick = true;
					break;
				}
				auto waitUntil = [&](Clock::time_point t) { cv_.wait_until(lock, autoOn ? std::min(t, autosaveDue_) : t); };
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
					waitUntil(when);
					continue;
				}
				/* rewind held goes back a frame a frame */
				if (s_ != nullptr && holdM_ == Hold::Rewind)
				{
					if (Clock::now() >= due_) { back = true; break; }
					waitUntil(due_);
					continue;
				}
				if (runningM_ && s_ != nullptr)
				{
					if (Clock::now() >= due_) { advance = true; break; }
					waitUntil(catchingM_ ? std::min(due_, catchDue_) : due_);
				}
				else
				{
					/* not running: the time until it runs again is not run time */
					pausedSincePassM_ = true;
					if (catchingM_)
						waitUntil(catchDue_);
					else if (autoOn)
						cv_.wait_until(lock, autosaveDue_);
					else
						cv_.wait(lock);
				}
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
		if (autosaveTick)
		{
			autosaveDue_ = Clock::now() + std::chrono::milliseconds(autosaveMsM_.load());
			later([this] { autosave(); });
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
			passOnMachine(0, false, true);
			/* TAStudio's rewind step, and its fast one while fast forwarding */
			if (frameM_ > 0) seekOnMachine(std::max<int64_t>(0, frameM_ - (fastM_ ? rewindStepFastM_.load() : rewindStepM_.load())));
			due_ += period_;
			if (Clock::now() - due_ > 4 * period_) due_ = Clock::now();
			continue;
		}
		if (advance)
		{
			passOnMachine(1, false, false);
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
	if (averageM_ != nullptr) ce_average_fps_destroy(averageM_);
	averageM_ = nullptr;
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
		projectSwitched();
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
		later([this, stem] {
			gameName.set(stem);
			/* a game opened by itself: the piano roll's columns as Chimera starts them */
			loadDefaultColumns();
		});
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

void SessionViewModel::onProjectWait(ProjectJob job)
{
	auto done = std::make_shared<std::promise<void>>();
	std::future<void> ran = done->get_future();
	submit([this, job = std::move(job), done] {
		job(projectM_);
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
		/* the piano roll's columns: the first roll's hidden ones, as
		 * Chimera's TAStudio saved them; none saved, its defaults */
		std::vector<std::string> hidden, held;
		bool saved = false;
		if (const json::Json *o = tastudio.find("o"))
			if (const json::Json *rolls = o->find("Columns"); rolls != nullptr && rolls->kind == json::Json::Kind::Array && !rolls->items.empty())
			{
				saved = true;
				for (const json::Json &c : rolls->items[0].items)
				{
					const json::Json *name = c.find("Name");
					const json::Json *visible = c.find("Visible");
					const json::Json *emphasis = c.find("Emphasis");
					if (name != nullptr && visible != nullptr && visible->kind == json::Json::Kind::Bool && !visible->boolean)
						hidden.push_back(name->text);
					if (name != nullptr && name->text != "CursorColumn" && name->text != "FrameColumn" && emphasis != nullptr &&
						emphasis->kind == json::Json::Kind::Bool && emphasis->boolean)
						held.push_back(name->text);
				}
			}
		later([this, held] { autoHeldColumnsLoaded.emit(held); });
		columnsLoadedM_ = saved;
		if (saved) later([this, hidden] { hiddenColumnsLoaded.emit(hidden); });
		else later([this] { loadDefaultColumns(); });
		/* the rest of TAStudio's part, for the view models that keep it */
		std::map<std::string, std::string> members;
		if (const json::Json *o = tastudio.find("o"))
			for (const auto &[k, v] : o->members) members[k] = json::write(v);
		tastudioMembersM_.clear();
		later([this, members] { tastudioLoaded.emit(members); });
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
		projectSwitched();
		lastAnswers_ = answers;
		if (!path.empty()) addRecent(path);
	});
}

void SessionViewModel::checkHistoryOnMachine(const std::string &keepIn)
{
	if (s_ == nullptr) return;
	auto say = [this](std::string text) { later([this, text] { status.set(text); }); };
	const int64_t last = ce_session_greenzone_nearest(s_, INT64_MAX);
	if (last <= 0)
	{
		say("The state history holds nothing to check yet");
		return;
	}
	/* From frame 0 as the history keeps it, as Chimera's check starts - and
	 * put there twice. Seen 2026-10-23 on the synthetic core: frame 0 put back
	 * from a machine far ahead keeps pages its state does not have (the
	 * machine saves two pages more from then on), while put back from a
	 * machine already on the history's own line it does not; the first
	 * comparison failed on that alone, with nothing desynced. The second
	 * time it is put back from a machine the history made. */
	movesM_++;
	for (int i = 0; i < 2; i++)
	{
		if (ce_session_greenzone_restore(s_, 0) != 0)
		{
			say("The state history check could not start: frame 0 is not stored");
			return;
		}
		if (i == 0 && stepOnMachine(false, true) < 0) return;
	}
	frameM_ = ce_session_frame(s_);
	auto saved = [this] {
		uint64_t len = 0;
		const uint8_t *d = ce_session_save_state(s_, &len);
		return d != nullptr ? std::vector<uint8_t>(d, d + len) : std::vector<uint8_t>();
	};
	int64_t lastState = 0, checked = 0;
	while (frameM_ < last)
	{
		/* each frame run as Frame Advance runs it, drawn: a machine whose
		 * picture is in its memory is not the same undrawn */
		if (stepOnMachine(false, true) < 0) return;
		const int64_t f = frameM_;
		if (ce_session_greenzone_nearest(s_, f) != f) continue;
		const std::vector<uint8_t> fresh = saved();
		ce_session_greenzone_restore(s_, f);
		const std::vector<uint8_t> kept = saved();
		checked++;
		if (fresh != kept)
		{
			std::string text = "The state history check failed: bad data between frames " + std::to_string(lastState) + " and " + std::to_string(f);
			if (!keepIn.empty())
			{
				std::error_code ec;
				std::filesystem::create_directories(keepIn, ec);
				std::ofstream(std::filesystem::path(keepIn) / "integrity.fresh", std::ios::binary)
					.write(reinterpret_cast<const char *>(fresh.data()), static_cast<std::streamsize>(fresh.size()));
				std::ofstream(std::filesystem::path(keepIn) / "integrity.greenzoned", std::ios::binary)
					.write(reinterpret_cast<const char *>(kept.data()), static_cast<std::streamsize>(kept.size()));
				text += "; both states are in " + keepIn;
			}
			report(-1, -1, -1, -1, -1);
			say(text);
			return;
		}
		lastState = f;
	}
	report(-1, -1, -1, -1, -1);
	say("The state history check passed: " + std::to_string(checked) + (checked == 1 ? " state" : " states") + " put the machine back exactly");
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

bool SessionViewModel::hiddenByDefault(const std::string &n)
{
	auto starts = [&n](const char *p) { return n.rfind(p, 0) == 0; };
	auto ends = [&n](const std::string &e) { return n.size() >= e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0; };
	return n == "Power" || n == "Reset" || n == "Light Sensor" || n == "Disc Select" || n == "Disk Index" || n == "Next Drive" ||
		n == "Next Slot" || n == "Insert Disk" || n == "Eject Disk" || starts("Tilt") || starts("Key ") || starts("Open") ||
		starts("Close") || ends("Tape") || ends("Disk") || ends("Block") || ends("Status");
}

void SessionViewModel::loadDefaultColumns()
{
	std::vector<std::string> hidden;
	for (const std::string &n : buttonNames_)
		if (hiddenByDefault(n)) hidden.push_back(n);
	for (const Axis &a : axes_)
		if (hiddenByDefault(a.name)) hidden.push_back(a.name);
	hiddenColumnsLoaded.emit(hidden);
}

void SessionViewModel::setAutoHeldColumns(const std::vector<std::string> &names)
{
	submit([this, names] { autoHeldColumnsM_ = names; });
}

void SessionViewModel::setColumnPatterns(std::vector<ColumnPattern> patterns)
{
	submit([this, patterns = std::move(patterns)] {
		columnPatternsM_ = patterns;
		columnAtM_.assign(columnPatternsM_.size(), 0);
	});
}

void SessionViewModel::setTastudioMember(const std::string &name, const std::string &json)
{
	submit([this, name, json] { tastudioMembersM_[name] = json; });
}

void SessionViewModel::setHiddenColumns(const std::vector<std::string> &names)
{
	/* a roll of Chimera's shape: its cursor and frame columns, then a column
	 * a control, named, its mnemonic for a label, as a new piano roll makes */
	json::Json roll;
	roll.kind = json::Json::Kind::Array;
	auto column = [&](const std::string &name, const std::string &text, bool visible, bool rotatable) {
		json::Json c;
		c.kind = json::Json::Kind::Object;
		c.members.emplace_back("VerticalWidth", json::Json::integer(10));
		c.members.emplace_back("HorizontalHeight", json::Json::integer(10));
		c.members.emplace_back("Left", json::Json::integer(0));
		c.members.emplace_back("Right", json::Json::integer(0));
		c.members.emplace_back("Name", json::Json::string(name));
		c.members.emplace_back("Text", json::Json::string(text));
		c.members.emplace_back("Visible", json::Json::boolean_(visible));
		c.members.emplace_back("Emphasis", json::Json::boolean_(false));
		c.members.emplace_back("Rotatable", json::Json::boolean_(rotatable));
		roll.items.push_back(c);
	};
	auto shown = [&names](const std::string &n) { return std::find(names.begin(), names.end(), n) == names.end(); };
	column("CursorColumn", "", true, false);
	column("FrameColumn", "Frame#", true, true);
	for (size_t b = 0; b < buttonNames_.size(); b++)
	{
		const std::string mn = b < mnemonics_.size() && mnemonics_[b] != '.' ? std::string(1, mnemonics_[b]) : buttonNames_[b];
		column(buttonNames_[b], mn, shown(buttonNames_[b]), false);
	}
	for (const Axis &a : axes_) column(a.name, a.header.empty() ? a.name : a.header, shown(a.name), true);
	const std::string fresh = json::write(roll);
	submit([this, names, fresh] {
		hiddenColumnsM_ = names;
		freshColumnsM_ = fresh;
	});
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
		projectSwitched();
		addRecent(path);
	});
}

bool SessionViewModel::saveProjectOnMachine(const std::string &to, bool backup)
{
	if (projectM_ == nullptr || s_ == nullptr) return false;
	const std::string path = to.empty() ? projectPathM_ : to;
	if (path.empty())
	{
		later([this] { status.set("Not saved: this project has no file yet; say where it goes"); });
		return false;
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
		/* the piano roll's columns: shown or hidden on the project's own
		 * entries (their widths and the rest left as Chimera saved them),
		 * else a roll of Chimera's shape */
		json::Json *rolls = nullptr;
		for (auto &[k, v] : o->members)
			if (k == "Columns" && v.kind == json::Json::Kind::Array && !v.items.empty()) rolls = &v;
		json::Json fresh;
		std::string ferr;
		const bool haveFresh = !freshColumnsM_.empty() && json::parse(freshColumnsM_, fresh, ferr);
		if (rolls != nullptr && haveFresh)
		{
			for (json::Json &roll : rolls->items)
				for (json::Json &c : roll.items)
				{
					const json::Json *name = c.find("Name");
					if (name == nullptr || name->text == "CursorColumn" || name->text == "FrameColumn") continue;
					const bool hide = std::find(hiddenColumnsM_.begin(), hiddenColumnsM_.end(), name->text) != hiddenColumnsM_.end();
					const bool emphasis = std::find(autoHeldColumnsM_.begin(), autoHeldColumnsM_.end(), name->text) != autoHeldColumnsM_.end();
					for (auto &[ck, cv] : c.members)
					{
						if (ck == "Visible") cv = json::Json::boolean_(!hide);
						if (ck == "Emphasis") cv = json::Json::boolean_(emphasis);
					}
				}
		}
		else if (haveFresh)
		{
			for (json::Json &c : fresh.items)
				if (const json::Json *name = c.find("Name"))
				{
					const bool emphasis = std::find(autoHeldColumnsM_.begin(), autoHeldColumnsM_.end(), name->text) != autoHeldColumnsM_.end();
					for (auto &[ck, cv] : c.members)
						if (ck == "Emphasis") cv = json::Json::boolean_(emphasis);
				}
			json::Json all;
			all.kind = json::Json::Kind::Array;
			all.items.push_back(fresh);
			o->members.emplace_back("Columns", all);
		}
		/* what the view models keep there, over what the project had */
		for (const auto &[name, text] : tastudioMembersM_)
		{
			json::Json value;
			std::string verr;
			if (!json::parse(text, value, verr)) continue;
			bool there = false;
			for (auto &[k, v] : o->members)
				if (k == name)
				{
					v = value;
					there = true;
				}
			if (!there) o->members.emplace_back(name, value);
		}
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
		return false;
	}
	if (backup)
	{
		later([this, path] { status.set("Backup saved: " + path); });
		return true;
	}
	const bool first = projectPathM_ != path;
	projectPathM_ = path;
	keepHistoryOnMachine();
	later([this, path, n, first] {
		projectPath.set(path);
		if (first) addRecent(path);
		status.set("Saved " + path + " (" + std::to_string(n) + " frames)");
	});
	return true;
}

void SessionViewModel::passOnMachine(int64_t frames, bool stepping, bool goingBack)
{
	/* as Chimera's AverageFpsPassBegins: the engine is told of each pass,
	 * and decides what counts; time spent paused never does */
	if (averageM_ == nullptr) averageM_ = ce_average_fps_create();
	if (averageM_ == nullptr) return;
	if (averageResetM_.exchange(false))
	{
		ce_average_fps_reset(averageM_);
		pausedSincePassM_ = true;
	}
	const auto now = Clock::now();
	if (passAt_ != Clock::time_point{})
	{
		const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - passAt_).count();
		ce_average_fps_pass(averageM_, frames, ns, pausedSincePassM_ ? 1 : 0, stepping ? 1 : 0, goingBack ? 1 : 0);
	}
	passAt_ = now;
	/* a pass not made running leaves the next one paused (what it waited is not run time) */
	pausedSincePassM_ = stepping || goingBack;
	const double average = ce_average_fps_value(averageM_);
	std::lock_guard<std::mutex> lock(pendingM_);
	pendingAverage_ = average;
}

void SessionViewModel::saveTo(const std::string &path)
{
	const uint64_t gen = changeGen_;
	submit([this, path, gen] {
		if (saveProjectOnMachine(path)) later([this, gen] { savedAt(gen); });
	});
}

std::string SessionViewModel::backupPathFor(const std::string &projectFile) const
{
	/* "walker.2026-10-23 06.26.46.chimeraProject" */
	const std::filesystem::path own(projectFile);
	const std::time_t now = std::time(nullptr);
	char when[32];
	std::strftime(when, sizeof when, "%Y-%m-%d %H.%M.%S", std::localtime(&now));
	std::error_code ec;
	std::filesystem::create_directories(backupFolder.get(), ec);
	return (std::filesystem::path(backupFolder.get()) / (own.stem().string() + "." + when + own.extension().string())).string();
}

void SessionViewModel::markChanged()
{
	if (!hasProject.get()) return;
	changeGen_++;
	unsavedM_.store(true);
	if (!unsaved.get()) unsaved.set(true);
}

void SessionViewModel::savedAt(uint64_t gen)
{
	/* a change made while the save was on its way is still unsaved */
	if (gen != changeGen_) return;
	unsavedM_.store(false);
	unsaved.set(false);
}

void SessionViewModel::projectSwitched()
{
	changeGen_++;
	unsavedM_.store(false);
	unsaved.set(false);
}

void SessionViewModel::autosave()
{
	/* as Chimera's: a project with a file of its own, changed since it was
	 * last saved; a backup leaves it unsaved, as Chimera's does */
	if (!hasProject.get() || projectPath.get().empty() || !unsaved.get()) return;
	if (autosaveAsBackup.get())
	{
		if (backupFolder.get().empty()) return;
		const std::string path = backupPathFor(projectPath.get());
		submit([this, path] { saveProjectOnMachine(path, true); });
	}
	else
		saveTo("");
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
	std::string controller = "Waterbox Controller";
	bool gameCore = false, movieImport = false;
	int64_t ms = 0;
	gameTimerM_ = ce_session_game_time_ms(s, &ms) == 1;
	const bool timer = gameTimerM_, saveData = ce_session_savedata_available(s) != 0;
	{
		json::Json cfg;
		std::string jerr;
		const bool read = json::parse(config, cfg, jerr);
		if (read)
			if (const json::Json *input = cfg.find("input"))
				if (const json::Json *n = input->find("name"); n != nullptr && !n->text.empty()) controller = n->text;
		if (read)
		{
			if (const json::Json *k = cfg.find("kind")) gameCore = k->text == "game";
			movieImport = cfg.find("movieImport") != nullptr;
		}
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
	/* another game, or the same started again: another average, as Chimera's */
	averageResetM_.store(true);
	later([this, names, axes, mnemonics, keybinds, coreText, systemText, rate, label, inputsReplaced, frames, controller,
			   gameCore, timer, saveData, movieImport, package] {
		controllerName_ = controller;
		packagePath_ = package;
		isGameCore.set(gameCore);
		hasGameTimer.set(timer);
		hasSaveData.set(saveData);
		hasMovieImport.set(movieImport);
		gameTimeText.set("");
		averageFpsText.set("");
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
		projectSwitched();
		status.set("Closed");
	});
}

void SessionViewModel::advanceOnMachine()
{
	const int64_t f = ce_session_frame(s_);
	const int64_t size = ce_session_movie_length(s_);
	stepOnMachine(true, true);
	/* past the end the movie grew: the project has a change */
	if (f >= size && !unsavedM_.exchange(true)) later([this] { markChanged(); });
	/* TAStudio's "automatically pause at end of movie": the frame the movie
	 * runs out on pauses, once; played on from there, it records */
	if (pauseAtEndM_.load())
	{
		const bool atEnd = ce_session_frame(s_) == ce_session_movie_length(s_);
		if (doPauseM_ && atEnd)
		{
			runningM_ = false;
			later([this] { running.set(false); });
		}
		doPauseM_ = !atEnd;
	}
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
		/* a pad's button pressed while autohold or autofire is armed changes over */
		const uint64_t padPressed = padMask & ~padWasM_;
		padWasM_ = padMask;
		if (padPressed != 0 && armed_ != 0)
		{
			toggleSticky(padPressed);
			later([this] { stickyChanged(); });
		}
		/* what autohold holds, and what autofire presses this frame */
		held |= autoholdMask_.load();
		const uint64_t firing = autofireMask_.load();
		const int64_t on = fireOnM_.load(), period = on + fireOffM_.load();
		for (size_t b = 0; b < 64; b++)
			if (firing >> b & 1 && fireAtM_[b] % period < on) held |= uint64_t{1} << b;
		/* the piano roll's Auto-hold columns: a button's pattern says whether it is pressed */
		for (size_t i = 0; i < columnPatternsM_.size(); i++)
		{
			const ColumnPattern &cp = columnPatternsM_[i];
			if (!cp.isAxis && !cp.values.empty() && cp.index >= 0 && cp.index < 64 && cp.values[columnAtM_[i] % cp.values.size()] != 0)
				held |= uint64_t{1} << cp.index;
		}
	}
	/* What is held goes into the movie before the frame plays it: past the
	 * end the movie grows, and over a frame it has, a press it lacks is an
	 * edit of that frame (the engine drops the states after it). The engine
	 * plays the movie; the window never hands the machine input of its own. */
	bool changed = false;
	held &= recordMaskM_.load();
	const size_t axisCount = static_cast<size_t>(ce_session_axis_count(s_));
	axesBuf_.resize(axisCount);
	uint64_t logged = 0;
	const int32_t had = ce_session_movie_get(s_, f, &logged, axisCount ? axesBuf_.data() : nullptr);
	if (had == 1 && !grow) return 0;
	/* an Auto-hold analog column: its value goes in as a held button does */
	bool axesHeld = false;
	if (live)
		for (size_t i = 0; i < columnPatternsM_.size(); i++)
		{
			const ColumnPattern &cp = columnPatternsM_[i];
			if (!cp.isAxis || cp.values.empty() || cp.index < 0 || static_cast<size_t>(cp.index) >= axisCount) continue;
			const int32_t v = cp.values[columnAtM_[i] % cp.values.size()];
			if (axesBuf_[static_cast<size_t>(cp.index)] != v)
			{
				axesBuf_[static_cast<size_t>(cp.index)] = v;
				axesHeld = true;
			}
		}
	if (had == 1)
		ce_session_movie_set(s_, f, held, axesHeld ? axesBuf_.data() : nullptr);
	else if ((logged | held) != logged || axesHeld)
	{
		ce_session_movie_set(s_, f, logged | held, axisCount ? axesBuf_.data() : nullptr);
		greenEndM_ = std::min(greenEndM_, f);
		changed = true;
	}

	const int32_t lag = ce_session_movie_advance(s_, 0, nullptr, render ? 1 : 0);
	/* autofire's patterns move on a frame, a lag frame not counted unless asked */
	if (live && lag >= 0 && (lag == 0 || !fireSkipsLagM_.load()))
	{
		const uint64_t firing = autofireMask_.load();
		for (size_t b = 0; b < 64; b++)
			if (firing >> b & 1) fireAtM_[b]++;
	}
	/* and the Auto-hold columns' patterns, each by its own rule for lag frames */
	if (live && lag >= 0)
		for (size_t i = 0; i < columnPatternsM_.size(); i++)
		{
			const ColumnPattern &cp = columnPatternsM_[i];
			if (cp.values.empty() || (lag != 0 && cp.skipLag)) continue;
			if (++columnAtM_[i] >= cp.values.size()) columnAtM_[i] = static_cast<size_t>(std::max<int64_t>(0, cp.loop));
		}
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
	/* the setting in force when the edit was made: one changed while the
	 * machine still has edits to get to does not change what they do */
	const bool restore = autoRestore.get();
	submit([this, first, sent, restore] {
		if (s_ == nullptr) return;
		editRunningM_ = sent;
		greenEndM_ = std::min(greenEndM_, first);
		/* moved since the last edit moved it: that edit's frame is forgotten */
		if (movesM_ != restoredMovesM_ || frameM_ != restoredAtM_) restoreFrameM_ = -1;
		const int64_t length = ce_session_movie_length(s_);
		/* an edit that gave a movie made shorter its frames again (an undo)
		 * lets the machine go on to where the first edit found it */
		const bool shortOfIt = restore && restoreFrameM_ > frameM_ && length > frameM_;
		if (frameM_ > first || shortOfIt)
		{
			/* The machine is past the edit. The engine honours it before the
			 * machine next moves; here the window says where: with autoRestore
			 * back on the frame it was on at the first edit, with the new
			 * inputs, else at the edit. Going back adds no frames to the
			 * movie: one made shorter is followed to its end. */
			if (restoreFrameM_ < 0) restoreFrameM_ = frameM_;
			seekOnMachine(restore ? restoreFrameM_ : first, false);
			restoredMovesM_ = movesM_;
			restoredAtM_ = frameM_;
		}
		report(-1, -1, -1, -1, -1);
	});
}

void SessionViewModel::report(int64_t cf, int64_t cl, int64_t af, int64_t al, double fpsNow)
{
	bool post = false;
	/* the game's own timer, when shown */
	int64_t gameMs = -1;
	if (s_ != nullptr && gameTimerM_ && showGameTimeM_.load())
	{
		int64_t ms = 0;
		if (ce_session_game_time_ms(s_, &ms) == 1) gameMs = ms;
	}
	/* the states among the frames shown: a few dozen lookups a report */
	const int64_t first = watchFirstM_.load(), count = std::min<int64_t>(watchCountM_.load(), 512);
	std::vector<uint8_t> stated(static_cast<size_t>(std::max<int64_t>(count, 0)), 0);
	if (s_ != nullptr)
		for (int64_t i = 0; i < count; i++) stated[static_cast<size_t>(i)] = ce_session_greenzone_nearest(s_, first + i) == first + i ? 1 : 0;
	{
		std::lock_guard<std::mutex> lock(pendingM_);
		pendingGameMs_ = gameMs;
		if (pendingStatedFrom_ != first || pendingStated_ != stated)
		{
			pendingStatedFrom_ = first;
			pendingStated_ = std::move(stated);
			statedFresh_ = true;
		}
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
	double measured, average;
	int64_t gameMs;
	int64_t queued, dropped;
	bool statedNow = false;
	{
		std::lock_guard<std::mutex> lock(pendingM_);
		tickPosted_ = false;
		if (statedFresh_)
		{
			statedFrom_ = pendingStatedFrom_;
			stated_ = pendingStated_;
			statedFresh_ = false;
			statedNow = true;
		}
		f = pendingFrame_;
		measured = pendingFps_;
		cf = changedFirst_, cl = changedLast_, af = appendedFirst_, al = appendedLast_;
		queued = pendingQueued_, dropped = pendingDropped_;
		green = pendingGreenEnd_, states = pendingStates_;
		rr = pendingRerecords_;
		average = pendingAverage_;
		gameMs = pendingGameMs_;
		pendingFps_ = -1;
		changedFirst_ = changedLast_ = appendedFirst_ = appendedLast_ = -1;
	}
	frame.set(f);
	if (measured >= 0) fps.set(measured);
	if (gameMs >= 0)
	{
		char text[48];
		ce_game_time_text(gameMs, text, sizeof text);
		gameTimeText.set(std::string("IGT ") + text);
	}
	if (showAverageFps.get() && average > 0)
	{
		char text[48];
		std::snprintf(text, sizeof text, "avg %.1f fps", average);
		averageFpsText.set(text);
	}
	soundQueued.set(queued);
	soundDropped.set(dropped);
	greenEnd.set(green);
	greenzoneStates.set(states);
	rerecords.set(rr);
	updateOverlay(f, cf >= 0 ? cf : af);
	if (cf >= 0) inputsChanged.emit(cf, cl);
	if (af >= 0) inputsAppended.emit(af, al);
	if (statedNow) statesShown.emit();
	screenChanged.emit();
}

void SessionViewModel::watchStates(int64_t first, int64_t count)
{
	if (first == watchFirstM_.load() && count == watchCountM_.load()) return;
	watchFirstM_.store(std::max<int64_t>(first, 0));
	watchCountM_.store(std::max<int64_t>(count, 0));
	/* answered now, not at the next frame run */
	submit([this] { report(-1, -1, -1, -1, -1); });
}

bool SessionViewModel::hasState(int64_t frame) const
{
	const int64_t i = frame - statedFrom_;
	return i >= 0 && i < static_cast<int64_t>(stated_.size()) && stated_[static_cast<size_t>(i)] != 0;
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
