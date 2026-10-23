#pragma once
/* The machine running: open a core package with a game, play, pause, go one
 * frame at a time, hold buttons. The machine runs on a thread of its own, so
 * a slow frame never holds up the window; what it learns is posted to the
 * view's thread and shows up here as properties. */

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "chimera/vm/bindings.hpp"
#include "chimera/vm/gamepads.hpp"
#include "chimera/vm/sound.hpp"
#include "chimera/vm/input_log.hpp"
#include "chimera/vm/view_model.hpp"

struct ce_session;
struct ce_project;
struct ce_average_fps;

namespace chimera::vm {

/* the files given in a zip, each stored as it is */
std::vector<uint8_t> storedZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files);

/* A named frame of the movie: a place to come back to. keepState asks the
 * greenzone to keep that frame's state whatever it thins. */
struct Marker
{
	int64_t frame = 0;
	std::string text;
	bool keepState = true;
	bool operator==(const Marker &) const = default;
};

class SessionViewModel : public ViewModel
{
public:
	SessionViewModel(Dispatcher &dispatcher, InputLog &inputs);
	~SessionViewModel() override;

	Property<bool> isOpen{false};
	/* how many times a machine has been opened: a view that keeps anything
	 * of the last one knows to start again */
	Property<int64_t> opens{0};
	Property<bool> running{false};
	/* the frame the machine is about to run (frames 0..frame-1 have run) */
	Property<int64_t> frame{0};
	Property<double> fps{0.0};
	Property<std::string> status{"Nothing open"};
	Property<std::string> coreName;
	Property<std::string> systemName;
	Property<int64_t> buttonCount{0};
	/* the gamepads in use, or why there are none */
	Property<std::string> gamepads{"No gamepads yet"};
	/* How fast the machine runs, as Chimera has it: a percent of its own
	 * rate (1 to 6400, in Chimera's steps), or as fast as it can
	 * (unthrottled); and its sound, on or off and its volume (0 to 100) */
	Property<int64_t> speedPercent{100};
	Property<bool> unthrottled{false};
	Property<bool> soundOn{true};
	Property<int64_t> volume{100};
	/* Chimera's fast forward runs at its alternate speed (400%), not as
	 * fast as it can; and its Speed/Skip: frames not drawn while running,
	 * 0 to 9 of every one drawn, only when behind when auto-minimized (as
	 * Chimera's, on by default); unthrottled draws one in four. Remembered */
	Property<int64_t> fastForwardPercent{400};
	/* Chimera's Sound window: the sound device ("" the system's), the buffer
	 * in milliseconds, the sound at the machine's own speed on or off, while
	 * fast forwarding on or off and its volume (a share of the volume), and
	 * frame advance muted; and the devices there are. Remembered */
	Property<std::string> soundDevice;
	Property<int64_t> soundBufferMs{100};
	Property<bool> soundNormalOn{true}, soundFastOn{true}, muteFrameAdvance{true};
	Property<int64_t> fastVolume{50};
	Property<std::string> soundDevices{"[]"};
	/* Chimera's greenzone budgets: what the history of states may keep in
	 * memory (MB, 64 at least), how close it stays behind the playhead (one
	 * frame in 1 to 32) and how many frames after a load show kept pictures
	 * (0 to 10); every project's, remembered. A project may ask for its own
	 * memory and closeness, kept in budgets.json beside its cache (a fact
	 * about this computer, not the project's file): projectBudgetMb and
	 * projectNearStride, 0 when it asks for nothing. What the machine open
	 * got: budgetMbApplied, nearStrideApplied. Each takes effect when a
	 * machine opens */
	Property<int64_t> greenzoneBudgetMb{4096}, greenzoneNearStride{4}, pictureSettleFrames{3};
	Property<int64_t> projectBudgetMb{0}, projectNearStride{0}, budgetMbApplied{0}, nearStrideApplied{0};
	/* Chimera's compile cache (docs/compile-cache.md): for a core that
	 * translates its machine's code (its package says "precompile"), what
	 * it compiled is kept between sessions in a folder for the game, named
	 * by the game's own SHA1 under compileCacheRoot; compileCacheDir is the
	 * open machine's, "" for none */
	Property<std::string> compileCacheRoot, compileCacheDir;
	static std::string compileDirFor(const std::string &root, bool compiles, const std::string &gameSha1);
	/* the volume a frame is heard at, by Chimera's rule: the volume when the
	 * sound is on; while fast forwarding a share of it, or none; none for a
	 * frame advanced by hand when that is muted */
	static int64_t frameVolume(bool soundOn, int64_t volume, bool fast, bool fastOn, int64_t fastVolume, bool advanced,
		bool muteAdvance);
	Property<int64_t> frameSkip{0};
	Property<bool> autoMinimizeSkipping{true};
	/* Chimera's Skip Lag Frames: a frame advance that lands after a lag
	 * frame runs on until one that is not (remembered) */
	Property<bool> skipLagFrames{false};
	/* Chimera's Display Nothing: the frames are run without being drawn, and
	 * the speed goes to the window's title (remembered) */
	Property<bool> displayNothing{false};
	/* Chimera's background input: a gamepad is heard while the window is
	 * not the one in front (remembered); windowActive says whether it is */
	Property<bool> acceptBackgroundInput{false};
	Property<bool> windowActive{true};
	/* What Chimera shows over the picture, each on or off (remembered): the
	 * speed ("60 fps", with >> fast forwarding and << rewinding), the frame
	 * counter (the frame, then the last frame with input, "(Finished)" past
	 * it), the lag counter (lag frames so far, and whether the frame just run
	 * was one), and the input of the frame just run, a mnemonic a button
	 * pressed, player by player and the console's last, as Chimera writes it */
	Property<bool> showFps{false}, showFrameCounter{false}, showLagCounter{false}, showInput{false};
	Property<std::string> fpsText{"0 fps"}, frameCounterText, inputText;
	Property<int64_t> lagCount{0};
	Property<bool> lagFrame{false};
	/* Chimera's View menu, the rest: the average speed since it was turned
	 * on or reset (counted by the engine: frames run freely over the time
	 * they took; "avg 59.9 fps", "" until something counted), shown on the
	 * speed's line; the rerecord count, 56 down; messages over the picture
	 * (the window shows each status a moment), and the status bar */
	Property<bool> showAverageFps{false}, showRerecords{false}, showMessages{true}, showStatusBar{true};
	Property<std::string> averageFpsText;
	/* Chimera's Emulator menu ("Game" for a game core) and what it offers
	 * for the core open: a game core's own timer, shown at the top right 14
	 * down ("IGT 01:02.345"); the user's save data, written out; a movie
	 * made elsewhere, imported */
	Property<bool> isGameCore{false}, hasGameTimer{false}, hasSaveData{false}, hasMovieImport{false};
	/* the core's tooling groups (the engine's): a trace of what its CPU runs,
	 * and registers a debugger shows; the trace's own header */
	Property<bool> hasTrace{false}, hasRegisters{false};
	Property<std::string> traceHeader;
	/* Tracing, as Chimera's trace logger asks it: on, the core's lines of
	 * each frame run are handed to `sink` on the window's thread ("[core:
	 * trace buffer full, lines dropped]" when it overflowed); off, the core
	 * traces nothing. Kept across machines and state loads */
	using TraceSink = std::function<void(std::vector<std::string> lines)>;
	void setTracing(bool on, TraceSink sink);
	/* the registers now, by the machine (the debugger's box): name, value, bits */
	struct Register
	{
		std::string name;
		int64_t value = 0;
		int32_t bits = 32;
	};
	std::vector<Register> readRegisters(int64_t &cycles, bool &hasCycles);
	/* a register set, by its place; false when the core does not take it */
	bool writeRegister(int32_t index, int64_t value);
	Property<bool> showGameTime{false};
	Property<std::string> gameTimeText;
	/* Chimera's Tools > Export Core Log: what the cores write, kept in a
	 * file only when asked and never remembered (each run starts with it
	 * off; the engine keeps it, ce_core_log). The file while it is on, ""
	 * while it is off */
	Property<std::string> coreLogPath;
	/* the input of a frame as the input display writes it */
	std::string inputDisplay(int64_t frame) const;
	/* Chimera's autohold and autofire: while one is armed (its hotkey held),
	 * a button newly pressed - a key's or a pad's - toggles being held, or
	 * firing: on for autofireOn frames, off for autofireOff, lag frames not
	 * counted unless autofireSkipsLag is off. Both last until cleared, and go
	 * into the frames the machine runs as a press held does. */
	Property<std::string> autoheld, autofired;
	/* as Chimera shows them while arming: "Held: P1 A Auto-P1 B " */
	Property<std::string> stickyText{"Held: "};
	Property<bool> stickyArmed{false};
	Property<int64_t> autofireOn{1}, autofireOff{1};
	Property<bool> autofireSkipsLag{true};
	void armAutohold(bool down);
	void armAutofire(bool down);
	/* A gamepad input caught for a binding (catchPad): waiting, and the
	 * name of the input pressed ("X1 A", "J1 POV0U"; "" while waiting) */
	Property<bool> catchingPad{false};
	Property<std::string> caughtPad;
	/* "Sound on", or why there is none */
	Property<std::string> sound{"No sound yet"};
	/* sample pairs sent to the speakers, and those dropped for running ahead */
	Property<int64_t> soundQueued{0};
	Property<int64_t> soundDropped{0};
	/* frames before this one were run with the inputs they have now */
	Property<int64_t> greenEnd{0};
	/* machine states the greenzone holds */
	Property<int64_t> greenzoneStates{0};
	/* how many times the movie was changed over work already done, saved
	 * with the project */
	Property<int64_t> rerecords{0};
	/* after an edit before the current frame, go back to the edit and run
	 * forward to where the machine was, with the new inputs */
	Property<bool> autoRestore{true};
	/* which frames the greenzone stores: 1 every frame, N one in N, 0 none
	 * (TAStudio's Greenzone box; saved with the project, 1 for one saved
	 * before it was), and the N the "every N frames" choice uses */
	Property<int64_t> greenzonePeriod{1};
	Property<int64_t> greenzoneEvery{2};
	/* how long the last seek took, in milliseconds, and how many frames it ran */
	Property<double> lastSeekMs{0.0};
	Property<int64_t> lastSeekFrames{0};
	/* the open project's file, "" when a game was opened without one or the
	 * project has not been saved yet; hasProject says which */
	Property<std::string> projectPath;
	Property<bool> hasProject{false};
	/* the open project's id (its cache folder's name), "" for none */
	Property<std::string> projectId;
	/* a project waiting for its files to be found: its files are the
	 * "projectFiles" rows, located one by one or by scanning a folder */
	Property<bool> resolvingFiles{false};
	/* Work a crash left behind (Chimera's ProjectRecovery): while a project
	 * has unsaved changes it is kept every five seconds in recoveryFolder,
	 * a folder per project, and let go when it is saved or closed cleanly.
	 * A project with no file yet has nowhere to be offered back, and is not
	 * kept.
	 * Found when the project opens again, it is rebuilt as a copy among the
	 * backups and offered: recoveredCopy names it ("" when nothing is
	 * offered), recoveredUpTo says when its last work was, and the open
	 * waits for the answer (recoverWork) */
	Property<std::string> recoveryFolder, recoveredCopy, recoveredUpTo;
	struct ProjectFile
	{
		std::string name, slot, sha1, actualSha1;
		/* 0 found and matching, 1 not found, 2 found but not the recorded bytes */
		int32_t status = 1;
	};
	const std::vector<ProjectFile> &projectFiles() const { return projectFiles_; }
	Property<std::string> projectTitle;
	/* the game's name: its file's, without the kind (a project's first file) */
	Property<std::string> gameName;
	/* where a project's backups go (Chimera's "Movie backups" path) */
	Property<std::string> backupFolder;
	/* projects opened, newest first, a line each (ten kept) */
	Property<std::string> recentProjects;
	/* the open project was changed since it was opened or last saved: its
	 * inputs, markers, branches or details (Chimera asks before closing it) */
	Property<bool> unsaved{false};
	/* TAStudio's settings for playing and saving: pause on the frame the
	 * movie runs out; how many frames a step of rewind goes back, and
	 * while fast forwarding; save the project by itself every so many
	 * seconds when it has changes (0 never), as a backup instead if asked;
	 * and a backup as well every time it is saved by hand */
	Property<bool> pauseAtEnd{false};
	Property<int64_t> rewindStep{1};
	Property<int64_t> rewindStepFast{4};
	Property<int64_t> autosaveSeconds{1800};
	Property<bool> autosaveAsBackup{false};
	Property<bool> backupOnSave{false};

	Picture screen;
	Signal<> screenChanged;

	/* the machine's buttons, and the one-letter name a movie gives each */
	const std::vector<std::string> &buttonNames() const { return buttonNames_; }
	/* the machine's analog controls, as its package declares them */
	struct Axis
	{
		std::string name, header;
		int64_t min = -32768, max = 32767, neutral = 0;
	};
	const std::vector<Axis> &axes() const { return axes_; }
	/* the controller's name as the package declares it (Chimera's
	 * ControllerDefinition name; "Waterbox Controller" when it says none) */
	const std::string &controllerName() const { return controllerName_; }
	/* the package the machine open runs ("" for none) */
	const std::string &packagePath() const { return packagePath_; }
	/* an input as Chimera's input display writes it: for each player in turn
	 * (1, 2... and the console's last), its buttons - the mnemonic of one
	 * pressed, a space for one not - then its analog controls, five places
	 * and a comma off neutral, six spaces at it */
	static std::string inputDisplay(const std::vector<std::string> &buttons, const std::string &mnemonics,
		const std::vector<Axis> &axes, uint64_t mask, const std::vector<int32_t> &values);
	/* each button's keys ("Down, J1 POV0D"), by index: as they are, as the
	 * package has them, and given again (the view's thread) */
	const std::vector<std::string> &bindingTexts() const { return bindings_.texts(); }
	const std::vector<std::string> &defaultBindingTexts() const { return defaultBindings_; }
	void setBindingTexts(std::vector<std::string> texts);
	const std::string &mnemonics() const { return mnemonics_; }
	Signal<> buttonsChanged;

	/* the inputs of frames first..last were changed, or frames were added
	 * at the end, by the machine (recording what was held) */
	Signal<int64_t, int64_t> inputsChanged;
	Signal<int64_t, int64_t> inputsAppended;
	/* every frame's inputs were replaced (a project opened) */
	Signal<> inputsReset;
	/* a project opened with these markers */
	Signal<std::vector<Marker>> markersLoaded;
	/* the piano roll's hidden columns, by control name, as the project (or
	 * Chimera's defaults, for one without its own) has them */
	Signal<std::vector<std::string>> hiddenColumnsLoaded;
	/* the hidden columns now, to save with the project in Chimera's shape */
	void setHiddenColumns(const std::vector<std::string> &names);
	/* TAStudio's other settings kept in a project (members of its TAStudio
	 * part, as Chimera's MovieClientSettings: "BoolPatterns"...): each as
	 * JSON text, when a project opens; and given back, to be saved with it
	 * (a member not given is saved as the project had it) */
	Signal<std::map<std::string, std::string>> tastudioLoaded;
	void setTastudioMember(const std::string &name, const std::string &json);
	/* The piano roll's Auto-hold columns (Chimera's column "emphasis"), by
	 * name: as the project had them, and the ones now, to save with it */
	Signal<std::vector<std::string>> autoHeldColumnsLoaded;
	void setAutoHeldColumns(const std::vector<std::string> &names);
	/* What each Auto-hold column presses while the game plays, on the UI's
	 * thread: its values in turn (1 or 0 for a button, the value for an
	 * analog control), back to `loop` after the last, a lag frame not
	 * counted when skipLag; a button or an analog control by its index */
	struct ColumnPattern
	{
		bool isAxis = false;
		int32_t index = 0;
		std::vector<int32_t> values;
		bool skipLag = true;
		int64_t loop = 0;
	};
	void setColumnPatterns(std::vector<ColumnPattern> patterns);
	/* Which frames the greenzone holds a state for, of those the piano
	 * roll shows: the UI's thread says which it shows, the machine answers
	 * with every report; hasState reads the last answer (false for a frame
	 * outside it), and statesShown says when it changed */
	void watchStates(int64_t first, int64_t count);
	bool hasState(int64_t frame) const;
	Signal<> statesShown;
	/* the controls Chimera's piano roll hides until shown (the console's
	 * power and reset, keyboard keys, disk and tape controls...) */
	static bool hiddenByDefault(const std::string &name);
	/* the movie's markers, from whoever edits them: saved with the project,
	 * and the frames of those that keep a state pinned in the greenzone */
	void setMarkers(std::vector<Marker> markers);

	/* A branch: another version of the movie, kept to come back to. */
	struct Branch
	{
		std::string name, time;
		int64_t frame = 0;
		/* the movie, as the engine writes it, and how many frames it has */
		std::string text;
		int64_t frames = 0;
		std::vector<Marker> markers;
		/* its state's file name in the project's cache folder, "" when it
		 * has none (it is then replayed to), and the picture it had */
		std::string state;
		std::shared_ptr<const Picture::Frame> picture;
		/* which branch this is while the window is open (not saved) */
		uint64_t id = 0;
	};
	/* a project opened with these branches */
	Signal<std::vector<Branch>> branchesLoaded;
	/* the branches to save with the project */
	void setBranches(std::vector<Branch> branches);
	/* another movie becomes the machine's, and the machine goes to `frame`:
	 * the engine keeps the states before the first frame where the two
	 * differ, and replays from there */
	void loadMovie(std::string text, int64_t frame);
	/* A branch of the movie as it is, made on the machine's thread: its
	 * frame, movie and picture, and with a project open its state kept in a
	 * file in the cache folder. done gets it on the view's thread, with why
	 * its state could not be kept ("" when it was, or there is no project). */
	void makeBranch(std::string name, std::string time, std::function<void(Branch, std::string why)> done);
	/* A branch put back: its movie, and the machine on its frame - by its
	 * state file when it has one this machine can take, else by replaying
	 * from where the two movies differ. done says which, on the view's
	 * thread, with why a state it had could not be loaded. */
	void loadBranch(Branch branch, std::function<void(bool fromState, std::string why)> done);
	/* a branch is gone, and its state file with it */
	void forgetBranch(Branch branch);

	/* where what a project can make again is kept (its branches' states), a
	 * folder per project; "" keeps nothing */
	Property<std::string> cacheFolder;

	/* Where packages and firmware are found (the cores view model's), asked
	 * on the machine's thread: a project that names no package runs the one
	 * found for its core, and every machine gets the firmware it needs. */
	using PackageFinder = std::function<std::string(const std::string &coreName, const std::string &sha1, std::string &note)>;
	using FolderFinder = std::function<std::vector<std::string>()>;
	/* the cores that take a file of this extension: {name, package} */
	using ExtensionFinder = std::function<std::vector<std::pair<std::string, std::string>>(const std::string &extension)>;
	void setFinders(PackageFinder packages, FolderFinder firmwareFolders, ExtensionFinder byExtension = {});

	/* a game waiting for its core to be chosen, and the cores it could be
	 * opened with, a line each ("" when nothing is waiting) */
	Property<std::string> chooseFor;
	Property<std::string> choices;

	/* for the window: a key went down or up, by Chimera's key names */
	void key(const std::string &name, bool down);

	/* Held keys, as Chimera's hotkeys work: frame advance runs a frame, and
	 * held half a second runs on until let go; rewind goes a frame back, and
	 * held goes on back a frame a frame; fast forward runs as fast as the
	 * machine can while held. */
	void holdFrameAdvance(bool down);
	void holdRewind(bool down);
	/* The UI's thread: another view model changed the open project (its
	 * markers, branches or details); it is unsaved until saved */
	void markChanged();
	/* The UI's thread: the buttons a key or pad held may write into the
	 * movie (a bit each; all by default) - the piano roll's columns shown,
	 * when hidden ones are not to be edited, as Chimera records */
	void setRecordMask(uint64_t mask) { recordMaskM_.store(mask); }
	void holdFastForward(bool down);
	/* whether a key presses any of the machine's buttons */
	bool keyIsBound(const std::string &name) const { return !bindings_.buttonsOf(name).empty(); }
	/* Chimera's Turbo, held: fast forward with nothing drawn or heard */
	void holdTurbo(bool down);
	bool turboHeld() const { return turboHeld_; }

	/* the inputs of frame `first` (and maybe later ones) were edited: the
	 * machine's states after it no longer hold */
	void edited(int64_t first);

	/* A memory domain's bytes, read on the machine's thread; this waits for
	 * it. Empty when there is no such domain or no machine. For tools and
	 * tests, not for every frame. */
	std::vector<uint8_t> readDomain(const std::string &name);

	/* On the machine's thread: the machine was changed by hand (a poke) at
	 * the frame it is on, so the stored states after it no longer hold */
	void dropStatesAfterOnMachine(int64_t frame);

	/* For other view models (RAM Search, RAM Watch, a hex editor) whose
	 * work is the machine's: run it on the machine's thread, with the open
	 * session or nullptr. */
	using MachineJob = std::function<void(ce_session *)>;
	void onMachine(MachineJob job);
	/* the same, and waits for it */
	void onMachineWait(MachineJob job);
	/* the open project, on the machine's thread (nullptr for none), waited
	 * for: what is changed in it is saved with it */
	using ProjectJob = std::function<void(ce_project *)>;
	void onProjectWait(ProjectJob job);
	/* What made the project last opened, kept after it closes, for New
	 * Project to open on as Chimera's does (closing a project is most often
	 * the step before changing one setting and going again): its core and
	 * the package it ran with, its settings, and where each of its files was
	 * found, as {slot, path}. Paths are this session's knowledge: a project
	 * carries names and hashes only. The window's thread. */
	struct ProjectAnswers
	{
		std::string coreName, package, settings;
		std::vector<std::pair<std::string, std::string>> files;
	};
	const ProjectAnswers &lastAnswers() const { return lastAnswers_; }
	/* From a job on the machine's thread: opens a project made in memory
	 * and never written (New Project's), taking it over. It has no file
	 * until it is first saved, which is when it is asked where it goes. */
	void openMadeProjectOnMachine(ce_project *p, const std::string &package);
	/* frame runs after every frame (seeks' too), closing just before a
	 * machine goes (its memory with it). removeMachineHooks waits until the
	 * machine's thread has let go of them. */
	int addMachineHooks(MachineJob frame, MachineJob closing);
	void removeMachineHooks(int id);

	/* Settings, for the settings view model. On the machine's thread: what
	 * the machine was opened with (a JSON object; the project's own when
	 * a project is open), and the slot map the settings' conditions read.
	 * Changing them is tried live; a core that cannot take them live is
	 * opened again with them, at frame 0, its inputs kept. The answer
	 * comes back on the view's thread: true when applied, with what
	 * happened. */
	std::string settingsOnMachine() const;
	std::string packageOnMachine() const { return package_; }
	std::string slotsOnMachine() const;
	void changeSettings(std::string json, std::function<void(bool, std::string)> done);

private:
	using Clock = std::chrono::steady_clock;

	void submit(std::function<void()> job);
	void loop();
	/* keepMovie: the machine starts again with the movie it has */
	void openOnMachine(const std::string &package, const std::string &game, bool keepMovie);
	std::string movieTextOnMachine() const;
	/* the movie replaced by this one; where the two part, or -1 */
	int64_t replaceMovieOnMachine(const std::string &text);
	/* the open project's folder in the cache, and its branch states' folder
	 * in that; empty when there is no project or no cache */
	std::filesystem::path projectCacheOnMachine() const;
	std::filesystem::path branchDirOnMachine() const;
	/* A project's session starts: the greenzone the last one kept is
	 * loaded when that session ended cleanly (not on a restart, whose
	 * movie may have been edited since the save), and the session is
	 * marked as running until it ends. */
	void startProjectSessionOnMachine(bool loadHistory);
	/* the greenzone kept with the project as it is saved, when it may
	 * outlive the session: written in the background */
	void keepHistoryOnMachine();
	/* who the project's cache belongs to, beside it ("about.json", as
	 * Chimera's ProjectCache writes it: title, the project's path, core,
	 * system, games), so the Cache Manager can name it */
	void rememberAboutOnMachine(const std::string &projectPath);
	/* which machine this is, as a state of it needs: core, settings, files */
	std::string machineIdOnMachine();
	/* the state files no branch of the project has any more go */
	void tidyBranchStatesOnMachine();
	static void loadMovieOnMachine(ce_session *s, const std::string &text);
	void openProjectOnMachine(const std::string &path, const std::string &package, bool keepInputs,
		ce_project *reuse = nullptr);
	/* the rest of opening a project, once its files are found; extraFlags
	 * lets a file that is not the recorded one be opened anyway */
	void bootProjectOnMachine(ce_project *p, const std::string &path, const std::string &packageGiven, bool keepInputs,
		ce_project *reuse, const std::string &kept, uint64_t keptRerecords, uint32_t extraFlags);
	/* a project waiting for its files to be found (Chimera's resolution
	 * window): what opening it was asked with */
	struct PendingOpen
	{
		ce_project *project = nullptr;
		std::string path, package;
		bool keepInputs = false;
		ce_project *reuse = nullptr;
		std::string kept;
		uint64_t keptRerecords = 0;
		/* the work a crash left, opened instead: unsaved once open */
		bool recovered = false;
	};
	/* the files looked for, then the rest of opening (or the project waits for its files) */
	void findFilesAndBootOnMachine(PendingOpen open);
	bool recoveredM_ = false;
	std::string recoveryFolderM_, backupFolderM_;
	std::filesystem::path recoveryDirOf(ce_project *p) const;
	/* the work kept for a crash, written now (quietly) or let go */
	void keepRecoveryOnMachine();
	void dropRecoveryOnMachine(ce_project *p);
	/* a crash's work for this project, rebuilt as a copy among the backups;
	 * the copy's path and the time of its last work, or false for none */
	bool recoveredCopyOnMachine(ce_project *p, const std::string &projectPath, std::string &copy, std::string &upTo);
	Clock::time_point recoveryDue_{};
	Connection recoveryWatch_, backupWatch_;
	PendingOpen pendingOpenM_;
	void publishProjectFilesOnMachine();
	/* where this machine found a project's files, kept in its cache folder
	 * (local-paths.json, as Chimera's ProjectLocalPaths keeps them): read
	 * as hints, each checked by its hash; written once they are found */
	void applyLocalPathsOnMachine(ce_project *p);
	void rememberLocalPathsOnMachine();
	std::filesystem::path cacheDirOf(ce_project *p) const;
	/* the project's file, first in the recent projects */
	void addRecent(const std::string &path);
	/* a backup: written there, the project's own file and greenzone left as they are */
	/* true when written */
	bool saveProjectOnMachine(const std::string &path, bool backup = false, bool quiet = false);
	/* the UI's thread: the project saved to its own file (or the one
	 * given) and, to back it up, a backup named for it and the time */
	void saveTo(const std::string &path);
	std::string backupPathFor(const std::string &projectFile) const;
	/* the changes counted since the project was opened; the count a save
	 * covered, once it is written */
	uint64_t changeGen_ = 0;
	void savedAt(uint64_t gen);
	void projectSwitched();
	void autosave();
	std::atomic<bool> unsavedM_{false}, pauseAtEndM_{false};
	std::atomic<uint64_t> recordMaskM_{~uint64_t{0}};
	std::atomic<int64_t> rewindStepM_{1}, rewindStepFastM_{4}, autosaveMsM_{1800000};
	Clock::time_point autosaveDue_{};
	/* the last frame run was not the movie's end (pausing there is once) */
	bool doPauseM_ = false;
	Connection pauseWatch_, rewindWatch_, rewindFastWatch_, autosaveWatch_;
	/* Chimera's state history check: from frame 0, at each frame the
	 * history holds, the machine saved, put back from the history and saved
	 * again, the two compared; the first that differ said, and written to
	 * `keepIn` when one is given */
	void checkHistoryOnMachine(const std::string &keepIn);
	/* a game's session made a project, as the New Project window makes one */
	void newProjectOnMachine(const std::string &path, const std::string &title);
	void freeProjectOnMachine();
	/* what every open shares, once the session exists */
	void adoptOnMachine(ce_session *s, const std::string &package, const std::string &label, bool inputsReplaced);
	void closeOnMachine();
	/* a frame run as the window runs it: drawn unless turbo or Display
	 * Nothing says not; its lag (1), or not (0), or below 0 when it failed */
	int32_t advanceOnMachine(bool skipDrawing = false);
	/* Chimera's throttle's frame skip: whether the frame about to run is
	 * left undrawn (its Throttle.Step) */
	bool skipNextOnMachine();
	int64_t framesToSkip_ = 0, lastSkipRate_ = 0;
	/* one frame: `live` takes the keys and pads held and plays the sound
	 * (a replay does neither); `render` makes the picture. The lag result. */
	/* runs one frame; grow false: a frame past the movie's end is not
	 * played (the window took it out since the machine was sent there) */
	int32_t stepOnMachine(bool live, bool render, bool grow = true);
	/* puts the machine on a frame; past the movie's end the movie grows to
	 * it, unless grow is false (going back after an edit): then it stops at
	 * the end */
	void seekOnMachine(int64_t target, bool grow = true);
	void report(int64_t changedFirst, int64_t changedLast, int64_t appendedFrom, int64_t appendedTo, double fpsNow);
	void tick();

	InputLog &inputs_;
	std::vector<std::string> buttonNames_;
	std::vector<Axis> axes_;
	std::string controllerName_, packagePath_;
	std::vector<ProjectFile> projectFiles_;
	class ProjectFileRows : public RowModel
	{
	public:
		explicit ProjectFileRows(SessionViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.projectFiles_.size()); }
		int32_t columnCount() const override { return 6; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		SessionViewModel &vm_;
	};
	ProjectFileRows projectFileRows_{*this};
	std::vector<std::string> defaultBindings_;
	std::string mnemonics_;
	Bindings bindings_;
	std::map<std::string, bool> keysDown_;

	/* the machine's thread; everything below it is that thread's alone,
	 * except what is atomic or behind pendingM_ */
	std::thread thread_;
	std::mutex m_;
	std::condition_variable cv_;
	std::deque<std::function<void()>> jobs_;
	bool quit_ = false;

	ce_session *s_ = nullptr;
	struct Hooks
	{
		int id;
		MachineJob frame, closing;
	};
	std::vector<Hooks> hooksM_;
	std::atomic<int> nextHook_{1};
	void freeSessionOnMachine();
	ce_project *projectM_ = nullptr;
	std::string projectPathM_;
	/* a game's settings (a project keeps its own) */
	std::string settingsM_;
	/* the firmware the open game was given, as a project records it */
	std::string firmwarePinsM_;
	/* the slot the open game went in (by its kind of file) */
	std::string gameSlotM_;
	std::vector<int32_t> axesBuf_;
	std::vector<Marker> markersM_;
	PackageFinder findPackageM_;
	ExtensionFinder byExtension_; /* the view's thread's */
	std::vector<std::pair<std::string, std::string>> offered_;
	FolderFinder firmwareFoldersM_;
	struct Firmware
	{
		std::vector<std::string> ids;
		std::vector<std::vector<uint8_t>> data;
		std::string pins; /* {"id","sha1"} of what was found, as a project records it */
	};
	/* the firmware a machine needs, read; false with why when some is missing */
	/* the firmware a machine needs, found: the package's and, for a package
	 * whose games have their own ("gameFirmware"), what the core says this
	 * game needs - asked again at every open, with the game (`game`, a path)
	 * and its other files ({name, path}), as Chimera asks */
	bool firmwareOnMachine(const std::string &package, const std::string &slots, const std::string &settings,
		const std::string &pins, const std::string &game, const std::vector<std::pair<std::string, std::string>> &files,
		Firmware &out, std::string &why);
	std::vector<Branch> branchesM_;
	std::string cacheFolderM_;
	Connection cacheFolderWatch_;
	std::string packageSha1M_;
	/* the mark that a project's session is running, removed when it ends */
	std::filesystem::path runningMarkM_;
	/* the machine died in this session: what it stored is not kept */
	bool diedM_ = false;

	void pinMarkersOnMachine();
	/* frames run since the machine opened: the engine counts only the
	 * frames of a movie it plays, and this plays frames by hand */
	int64_t frameM_ = 0;
	int64_t greenEndM_ = 0;
	std::atomic<int64_t> speedM_{100}, volumeM_{100}, fastSpeedM_{400}, frameSkipM_{0};
	std::atomic<bool> autoMinimizeM_{true}, soundNormalOnM_{true}, soundFastOnM_{true}, muteAdvanceM_{true}, steppingM_{false};
	std::atomic<int64_t> fastVolumeM_{50}, budgetMbM_{4096}, nearStrideM_{4}, pictureSettleM_{3};
	/* a project's own budgets, from budgets.json in its cache folder: 0 for none */
	void readBudgetsOnMachine(ce_project *p, int64_t &memoryMb, int64_t &nearStride) const;
	Connection budgetWatch_, strideWatch_, settleWatch_;
	std::string soundDeviceM_, compileRootM_;
	bool traceOnM_ = false;
	TraceSink traceSink_;
	/* the lines the core traced since the last drain, to the sink */
	void drainTraceOnMachine();
	/* the compile cache folder for the session about to open: the engine's
	 * (ce_cache_dir) is the whole process's, so it is set before each open */
	void compileCacheOnMachine(const std::string &package, const std::string &gamePath, const std::string &knownSha1);
	Connection compileRootWatch_;
	int64_t soundBufferMsM_ = 100;
	/* the sound opened again on the device and buffer asked for; the devices said */
	void reopenSoundOnMachine();
	Connection soundDeviceWatch_, soundBufferWatch_, soundNormalWatch_, soundFastWatch_, muteAdvanceWatch_, fastVolumeWatch_;
	Connection fastSpeedWatch_, frameSkipWatch_, autoMinimizeWatch_;
	std::atomic<bool> unthrottledM_{false}, soundOnM_{true}, turboM_{false}, skipLagM_{false}, displayNothingM_{false},
		padsHeardM_{true};
	bool turboHeld_ = false;
	Connection skipLagWatch_, displayNothingWatch_, backgroundWatch_, activeWatch_;
	Connection speedWatch_, unthrottledWatch_, soundOnWatch_, volumeWatch_;
	/* Where an edit sends the machine back to, as Chimera's TAStudio does: the
	 * frame it was on at the first edit, kept through the edits that follow
	 * until the machine is moved some other way (-1: none). movesM_ counts
	 * the machine's moves; restoredMovesM_ and restoredAtM_ say how many there
	 * had been, and where it was, when the last edit had moved it. */
	int64_t restoreFrameM_ = -1, restoredAtM_ = -1;
	uint64_t movesM_ = 0, restoredMovesM_ = 0;
	/* edits the window has made (counted on its thread as each is sent), and
	 * the count the edit job now running was sent at: more since means a
	 * later edit's job is on its way */
	std::atomic<uint64_t> editsSent_{0};
	ProjectAnswers lastAnswers_;
	/* the hidden columns and the piano roll's columns, in Chimera's shape
	 * (written when the project had none of its own), for the machine's thread */
	std::vector<std::string> hiddenColumnsM_;
	std::map<std::string, std::string> tastudioMembersM_;
	std::vector<std::string> autoHeldColumnsM_;
	/* the Auto-hold columns' patterns, and where each is */
	std::vector<ColumnPattern> columnPatternsM_;
	/* the frames the piano roll shows, and the states among them */
	std::atomic<int64_t> watchFirstM_{0}, watchCountM_{0};
	int64_t pendingStatedFrom_ = 0;
	std::vector<uint8_t> pendingStated_;
	bool statedFresh_ = false;
	int64_t statedFrom_ = 0;
	std::vector<uint8_t> stated_;
	std::vector<size_t> columnAtM_;
	std::string freshColumnsM_;
	bool columnsLoadedM_ = false;
	/* the defaults, sent once a machine's controls are known */
	void loadDefaultColumns();
	/* what the counters over the picture last worked out, so a frame run
	 * costs a frame's work: the last frame with input (for a movie of this
	 * length), and the lag frames counted up to a frame */
	void updateOverlay(int64_t frame, int64_t changedFrom);
	int64_t lastInputFor_ = -1, lastInput_ = 0, lagsTo_ = 0, lags_ = 0;
	bool fastHeld_ = false, rewindHeld_ = false;
	std::vector<Connection> overlayWatches_;
	uint64_t editRunningM_ = 0;
	bool runningM_ = false;
	/* what a held key is doing (the machine's thread) */
	enum class Hold { None, Advance, Rewind };
	Hold holdM_ = Hold::None;
	Clock::time_point holdSince_{};
	bool holdRunning_ = false, fastM_ = false;
	Clock::time_point due_{};
	Clock::duration period_ = std::chrono::microseconds(16667);
	int64_t fpsFrames_ = 0;
	Clock::time_point fpsSince_{};
	std::string package_, game_;
	/* gamepads are read on this thread, just before each frame */
	std::unique_ptr<Gamepads> padsM_;
	/* catching a pad input: when to look next, and what was held already */
	bool catchingM_ = false;
	Clock::time_point catchDue_{};
	std::vector<std::string> catchHeldM_;
	void catchPollOnMachine();
	Bindings bindingsM_;
	Connection periodWatch_;
	std::atomic<int64_t> periodM_{1};
	std::vector<std::string> padHeld_;
	size_t padCount_ = 0;
	std::unique_ptr<Sound> soundM_;

	std::atomic<uint64_t> held_{0};
	/* the sticky buttons, a bit each, and which arming is on (1 hold, 2 fire) */
	std::atomic<uint64_t> autoholdMask_{0}, autofireMask_{0};
	std::atomic<int> armed_{0};
	std::atomic<int64_t> fireOnM_{1}, fireOffM_{1};
	std::atomic<bool> fireSkipsLagM_{true};
	/* each firing button's place in its pattern, and the pads' buttons held
	 * at the last frame (a pad's press arms a toggle too) - the machine's */
	std::array<int64_t, 64> fireAtM_{};
	uint64_t padWasM_ = 0;
	void toggleSticky(uint64_t pressed);
	void stickyChanged();
	std::vector<Connection> stickyWatches_;

	/* what the next tick on the view's thread reports */
	std::mutex pendingM_;
	bool tickPosted_ = false;
	int64_t pendingFrame_ = 0;
	double pendingFps_ = -1;
	/* the average speed: the engine's count, what the loop's last pass did */
	ce_average_fps *averageM_ = nullptr;
	Clock::time_point passAt_{};
	bool pausedSincePassM_ = true;
	void passOnMachine(int64_t frames, bool stepping, bool goingBack);
	double pendingAverage_ = 0;
	std::atomic<bool> averageResetM_{false};
	/* the game's timer, read with each report when shown */
	std::atomic<bool> showGameTimeM_{false};
	bool gameTimerM_ = false;
	int64_t pendingGameMs_ = -1;
	int64_t pendingQueued_ = 0, pendingDropped_ = 0;
	int64_t pendingGreenEnd_ = 0, pendingStates_ = 0, pendingRerecords_ = 0;
	int64_t changedFirst_ = -1, changedLast_ = -1;
	int64_t appendedFirst_ = -1, appendedLast_ = -1;
};

} // namespace chimera::vm
