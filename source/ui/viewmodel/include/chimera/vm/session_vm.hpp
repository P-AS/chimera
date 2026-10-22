#pragma once
/* The machine running: open a core package with a game, play, pause, go one
 * frame at a time, hold buttons. The machine runs on a thread of its own, so
 * a slow frame never holds up the window; what it learns is posted to the
 * view's thread and shows up here as properties. */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
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

namespace chimera::vm {

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
	/* how long the last seek took, in milliseconds, and how many frames it ran */
	Property<double> lastSeekMs{0.0};
	Property<int64_t> lastSeekFrames{0};
	/* the open project, "" when a game was opened without one */
	Property<std::string> projectPath;
	Property<std::string> projectTitle;
	/* projects opened, newest first, a line each (ten kept) */
	Property<std::string> recentProjects;

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
	void holdFastForward(bool down);

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
	/* which machine this is, as a state of it needs: core, settings, files */
	std::string machineIdOnMachine();
	/* the state files no branch of the project has any more go */
	void tidyBranchStatesOnMachine();
	static void loadMovieOnMachine(ce_session *s, const std::string &text);
	void openProjectOnMachine(const std::string &path, const std::string &package, bool keepInputs,
		ce_project *reuse = nullptr);
	void saveProjectOnMachine(const std::string &path);
	/* a game's session made a project, as the New Project window makes one */
	void newProjectOnMachine(const std::string &path, const std::string &title);
	void freeProjectOnMachine();
	/* what every open shares, once the session exists */
	void adoptOnMachine(ce_session *s, const std::string &package, const std::string &label, bool inputsReplaced);
	void closeOnMachine();
	void advanceOnMachine();
	/* one frame: `live` takes the keys and pads held and plays the sound
	 * (a replay does neither); `render` makes the picture. The lag result. */
	int32_t stepOnMachine(bool live, bool render);
	void seekOnMachine(int64_t target);
	void report(int64_t changedFirst, int64_t changedLast, int64_t appendedFrom, int64_t appendedTo, double fpsNow);
	void tick();

	InputLog &inputs_;
	std::vector<std::string> buttonNames_;
	std::vector<Axis> axes_;
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
	bool firmwareOnMachine(const std::string &package, const std::string &slots, const std::string &settings,
		const std::string &pins, Firmware &out, std::string &why);
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
	std::atomic<bool> autoRestoreM_{true};
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
	Bindings bindingsM_;
	Connection autoRestoreWatch_;
	std::vector<std::string> padHeld_;
	size_t padCount_ = 0;
	std::unique_ptr<Sound> soundM_;

	std::atomic<uint64_t> held_{0};

	/* what the next tick on the view's thread reports */
	std::mutex pendingM_;
	bool tickPosted_ = false;
	int64_t pendingFrame_ = 0;
	double pendingFps_ = -1;
	int64_t pendingQueued_ = 0, pendingDropped_ = 0;
	int64_t pendingGreenEnd_ = 0, pendingStates_ = 0, pendingRerecords_ = 0;
	int64_t changedFirst_ = -1, changedLast_ = -1;
	int64_t appendedFirst_ = -1, appendedLast_ = -1;
};

} // namespace chimera::vm
