#include "chimera/vm/chimera_settings.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>

namespace chimera::vm {

using json::Json;
using Kind = ChimeraSetting::Kind;

const std::vector<ChimeraSetting> &chimeraSettings()
{
	static const std::vector<ChimeraSetting> all = {
		{"SpeedPercent", Kind::Plain, "session.speedPercent", ""},
		{"SpeedPercentAlternate", Kind::Plain, "session.fastForwardPercent", ""},
		{"ClockThrottle", Kind::Throttle, "session.throttle", "clock"},
		{"SoundThrottle", Kind::Throttle, "session.throttle", "audio"},
		{"VSyncThrottle", Kind::Throttle, "session.throttle", "vsync"},
		{"Unthrottled", Kind::Plain, "session.unthrottled", ""},
		{"UnthrottledDrawOneIn", Kind::Plain, "session.fastDrawOneIn", ""},
		{"VSync", Kind::Plain, "session.vsyncEnabled", ""},
		{"TurboSeek", Kind::Plain, "session.turboSeek", ""},
		{"SkipLagFrame", Kind::Plain, "session.skipLagFrames", ""},
		{"AcceptBackgroundInput", Kind::Plain, "session.acceptBackgroundInput", ""},
		{"CaptureMouse", Kind::Plain, "session.captureMouse", ""},
		{"ScreenshotCaptureOsd", Kind::Plain, "session.captureOsd", ""},
		{"AutofireOn", Kind::Plain, "session.autofireOn", ""},
		{"AutofireOff", Kind::Plain, "session.autofireOff", ""},
		{"AutofireLagFrames", Kind::Plain, "session.autofireSkipsLag", ""},
		{"DisplayFps", Kind::Plain, "session.showFps", ""},
		{"DisplayAverageFps", Kind::Plain, "session.showAverageFps", ""},
		{"DisplayFrameCounter", Kind::Plain, "session.showFrameCounter", ""},
		{"DisplayLagCounter", Kind::Plain, "session.showLagCounter", ""},
		{"DisplayInput", Kind::Plain, "session.showInput", ""},
		{"DisplayRerecordCount", Kind::Plain, "session.showRerecords", ""},
		{"DisplayGameTime", Kind::Plain, "session.showGameTime", ""},
		{"DisplayMessages", Kind::Plain, "session.showMessages", ""},
		{"DispChromeStatusBarWindowed", Kind::Plain, "session.showStatusBar", ""},
		{"DispSpeedupFeatures", Kind::DisplayFeatures, "session.displayNothing", "display.minimalDisplay"},
		{"SoundEnabled", Kind::Plain, "session.soundOn", ""},
		{"SoundEnabledNormal", Kind::Plain, "session.soundNormalOn", ""},
		{"SoundEnabledRWFF", Kind::Plain, "session.soundFastOn", ""},
		{"MuteFrameAdvance", Kind::Plain, "session.muteFrameAdvance", ""},
		{"SoundVolume", Kind::Plain, "session.volume", ""},
		{"SoundVolumeRWFF", Kind::Plain, "session.fastVolume", ""},
		{"SoundDevice", Kind::Plain, "session.soundDevice", ""},
		{"SoundBufferSizeMs", Kind::Plain, "session.soundBufferMs", ""},
		{"Movies.GreenzoneBudgetMb", Kind::Plain, "session.greenzoneBudgetMb", ""},
		{"Movies.GreenzoneMaxNearStride", Kind::Plain, "session.greenzoneNearStride", ""},
		{"Movies.GreenzonePictureSettleFrames", Kind::Plain, "session.pictureSettleFrames", ""},
		{"StartPaused", Kind::Plain, "customize.startPaused", ""},
		{"StartFullscreen", Kind::Plain, "customize.startFullscreen", ""},
		{"SuppressAskSave", Kind::Plain, "customize.neverAskSave", ""},
		{"RunLuaDuringTurbo", Kind::Plain, "customize.luaDuringTurbo", ""},
		{"ShowContextMenu", Kind::Plain, "customize.showContextMenu", ""},
		{"HandleAlternateKeyboardLayouts", Kind::Plain, "customize.alternateLayouts", ""},
		{"MergeLAndRModifierKeys", Kind::Plain, "customize.mergeModifiers", ""},
		{"SingleInstanceMode", Kind::Plain, "customize.singleInstance", ""},
		{"GCAdapterSupportEnabled", Kind::Plain, "customize.gameCubeAdapter", ""},
		{"StackOSDMessages", Kind::Plain, "messages.stackMessages", ""},
		{"OSDMessageDuration", Kind::Plain, "messages.messageSeconds", ""},
		{"MessagesColor", Kind::Colour, "messages.messagesColor", ""},
		{"AlertMessageColor", Kind::Colour, "messages.alertColor", ""},
		{"LastInputColor", Kind::Colour, "messages.lastInputColor", ""},
		{"MovieInputColor", Kind::Colour, "messages.movieInputColor", ""},
		{"Fps", Kind::Place, "messages.positions", "Fps"},
		{"FrameCounter", Kind::Place, "messages.positions", "Frame Counter"},
		{"LagCounter", Kind::Place, "messages.positions", "Lag Counter"},
		{"InputDisplay", Kind::Place, "messages.positions", "Input Display"},
		{"RamWatches", Kind::Place, "messages.positions", "Watches"},
		{"Messages", Kind::Place, "messages.positions", "Messages"},
		{"ReRecordCounter", Kind::Place, "messages.positions", "Rerecords"},
		{"Autohold", Kind::Place, "messages.positions", "Autohold"},
		{"GameTime", Kind::Place, "messages.positions", "Game Time"},
		{"DispFinalFilter", Kind::Plain, "display.finalFilter", ""},
		{"DispFixAspectRatio", Kind::Plain, "display.letterbox", ""},
		{"DispFixScaleInteger", Kind::Plain, "display.integerScale", ""},
		{"DispManagerAR", Kind::Choice, "display.aspectMode", "None|System|CustomSize|CustomRatio"},
		{"DispCustomUserARWidth", Kind::Plain, "display.customWidth", ""},
		{"DispCustomUserARHeight", Kind::Plain, "display.customHeight", ""},
		{"DispCustomUserArx", Kind::Plain, "display.ratioX", ""},
		{"DispCustomUserAry", Kind::Plain, "display.ratioY", ""},
		{"DispCropLeft", Kind::Plain, "display.cropLeft", ""},
		{"DispCropTop", Kind::Plain, "display.cropTop", ""},
		{"DispCropRight", Kind::Plain, "display.cropRight", ""},
		{"DispCropBottom", Kind::Plain, "display.cropBottom", ""},
		{"DispPrescale", Kind::Plain, "display.prescale", ""},
		{"DispAutoPrescale", Kind::Plain, "display.autoPrescale", ""},
		{"DispFullscreenHacks", Kind::Plain, "display.fullscreenHacks", ""},
		{"DispChromeFrameWindowed", Kind::Plain, "display.frameWindowed", ""},
		{"DispChromeCaptionWindowed", Kind::Plain, "display.captionWindowed", ""},
		{"DispChromeMenuWindowed", Kind::Plain, "display.menuWindowed", ""},
		{"DispChromeStatusBarFullscreen", Kind::Plain, "display.statusBarFullscreen", ""},
		{"DispChromeMenuFullscreen", Kind::Plain, "display.menuFullscreen", ""},
		{"DispChromeFullscreenAutohideMouse", Kind::Plain, "display.autohideMouseFullscreen", ""},
		{"DispChromeAllowDoubleClickFullscreen", Kind::Plain, "display.allowDoubleClickFullscreen", ""},
		{"ScaleOSDWithSystemScale", Kind::Plain, "display.scaleOsd", ""},
		{"ResizeWithFramebuffer", Kind::Plain, "display.resizeWithPicture", ""},
		{"SaveWindowPosition", Kind::Plain, "display.saveWindowPosition", ""},
		{"MainFormStayOnTop", Kind::Plain, "display.stayOnTop", ""},
		{"MainFormMouseCaptureForcesTopmost", Kind::Plain, "display.mouseCaptureForcesTop", ""},
		{"UseStaticWindowTitles", Kind::Plain, "display.staticWindowTitles", ""},
		{"MainFormStaticWindowTitleOverride", Kind::Plain, "display.titleSuffix", ""},
		{"AviCaptureOsd", Kind::Plain, "encoder.captureOsd", ""},
		{"AviCaptureLua", Kind::Plain, "encoder.captureLua", ""},
		{"VideoWriterAudioSync", Kind::Plain, "encoder.audioSync", ""},
		{"AVWriterPad", Kind::Plain, "encoder.pad", ""},
		{"AVWriterResizeWidth", Kind::Resize, "encoder.resize", "encoder.width"},
		{"AVWriterResizeHeight", Kind::Resize, "encoder.resize", "encoder.height"},
		{"FFmpegCustomCommand", Kind::Plain, "encoder.command", ""},
		{"InputHotkeyOverrideOptions", Kind::Choice, "hotkeys.keyPriority", "BOTH|INPUT|HOTKEY"},
		{"NoMixedInputHokeyOverride", Kind::Plain, "hotkeys.noMixedPriority", ""},
		{"HotkeyConfigAutoTab", Kind::Plain, "hotkeys.autoTab", ""},
		{"InputConfigAutoTab", Kind::Plain, "controls.autoTab", ""},
		{"DisplayWatchesOnScreen", Kind::Plain, "ramWatch.showOnScreen", ""},
		{"RamWatchDefinePrevious", Kind::Choice, "ramWatch.previousType", "Original|LastSearch|LastFrame|LastChange"},
		{"CacheAutoClean", Kind::Plain, "cacheManager.autoClean", ""},
		{"CacheSizeLimitMb", Kind::Megabytes, "cacheManager.limitGb", ""},
		{"CacheFreeSpaceFloorMb", Kind::Megabytes, "cacheManager.floorGb", ""},
		{"CoresFolder", Kind::Plain, "cores.coresFolder", ""},
		{"CoreManagerShows", Kind::Choice, "cores.shows", "All|Emulators|Games"},
		{"FirmwareShows", Kind::Choice, "firmware.shows", "All|Emulators|Games"},
		{"NewProjectKind", Kind::CoreKind, "newProject.kind", ""},
		{"DataDirectory", Kind::Plain, "dataFolder.current", ""},
		{"DataDirectoryPending", Kind::NullWhenEmpty, "dataFolder.pending", ""},
		{"DataDirectoryPendingMove", Kind::Plain, "dataFolder.pendingMove", ""},
		{"Theme", Kind::Plain, "themes.current", ""},
		{"DefaultAuthor", Kind::Plain, "projectDetails.defaultAuthor", ""},
		{"DisplaySubtitles", Kind::Plain, "projectDetails.showSubtitles", ""},
		{"CompileSessionGigabytes", Kind::Plain, "precompiled.sessionGigabytes", ""},
		{"SnowyNullCore", Kind::Choice, "idleEye.snowWhen", "Never|Always|WeekOfChristmas"},
	};
	return all;
}

std::string chimeraSettingsText()
{
	static const char *const kinds[] = {"plain", "choice", "corekind", "colour", "megabytes", "throttle", "resize",
		"displayfeatures", "nullwhenempty", "place"};
	std::string out;
	for (const ChimeraSetting &s : chimeraSettings())
		out += std::string(s.name) + "\t" + kinds[static_cast<int>(s.kind)] + "\t" + s.key + "\t" + s.extra + "\n";
	return out;
}

bool isChimeraSettings(const Json &root)
{
	if (root.kind != Json::Kind::Object) return false;
	/* the window's own are "view model.property" */
	for (const auto &[name, value] : root.members)
		if (name.find('.') != std::string::npos) return false;
	return root.find("HotkeyBindings") != nullptr || root.find("LastWrittenFromDetailed") != nullptr;
}

namespace {
/* a member by Chimera's name, "Movies.GreenzoneBudgetMb" inside Movies */
const Json *byName(const Json &root, const std::string &name)
{
	const size_t dot = name.find('.');
	if (dot == std::string::npos) return root.find(name);
	const Json *outer = root.find(name.substr(0, dot));
	return outer != nullptr ? outer->find(name.substr(dot + 1)) : nullptr;
}

/* the window's setting, set once whatever entries reach it */
void put(Json &settings, const std::string &key, Json value)
{
	for (auto &[k, v] : settings.members)
		if (k == key)
		{
			v = std::move(value);
			return;
		}
	settings.members.emplace_back(key, std::move(value));
}

bool isScalar(const Json &v) { return v.kind == Json::Kind::Bool || v.kind == Json::Kind::Number || v.kind == Json::Kind::String; }

int64_t whole(const Json &v) { return static_cast<int64_t>(std::llround(v.number())); }

const Json *objectAt(const Json *in, const char *name)
{
	const Json *v = in != nullptr ? in->find(name) : nullptr;
	return v != nullptr && v->kind == Json::Kind::Object ? v : nullptr;
}

Json object()
{
	Json o;
	o.kind = Json::Kind::Object;
	return o;
}

/* Chimera's recent files ({"recentlist": [...]}, newest first) as the
 * window's, a line each; false when it is not one */
bool recentLines(const Json *recent, std::string &out)
{
	const Json *list = recent != nullptr ? recent->find("recentlist") : nullptr;
	if (list == nullptr || list->kind != Json::Kind::Array) return false;
	out.clear();
	for (const Json &e : list->items)
		if (e.kind == Json::Kind::String) out += (out.empty() ? "" : "\n") + e.text;
	return true;
}

/* "30, 30, 30" (System.Drawing's) as "#1E1E1E"; "" when it is not one */
std::string rgb(const Json *v)
{
	int r = 0, g = 0, b = 0;
	if (v == nullptr || v->kind != Json::Kind::String || std::sscanf(v->text.c_str(), "%d , %d , %d", &r, &g, &b) != 3) return {};
	char hex[8];
	std::snprintf(hex, sizeof hex, "#%02X%02X%02X", r & 0xFF, g & 0xFF, b & 0xFF);
	return hex;
}

/* "Arial, 8.25pt, style=Bold" (System.Drawing's) as the window's
 * {"family", "pointSize", "bold", "italic"}; "" when it is not one */
std::string font(const Json *v)
{
	if (v == nullptr || v->kind != Json::Kind::String) return {};
	const std::string &t = v->text;
	const size_t comma = t.find(',');
	double size = 0;
	if (comma == std::string::npos || std::sscanf(t.c_str() + comma + 1, " %lfpt", &size) != 1 || size <= 0) return {};
	const size_t style = t.find("style=");
	const std::string styles = style == std::string::npos ? std::string() : t.substr(style);
	Json f = object();
	f.members.emplace_back("family", Json::string(t.substr(0, comma)));
	f.members.emplace_back("pointSize", Json::number(size));
	f.members.emplace_back("bold", Json::boolean_(styles.find("Bold") != std::string::npos));
	f.members.emplace_back("italic", Json::boolean_(styles.find("Italic") != std::string::npos));
	return json::write(f);
}

/* the settings that are more than one value: bindings, paths, recent files,
 * firmware chosen, the windows' places and the tools' own settings. Each of
 * Chimera's names taken goes in `carried` */
void carryRich(const Json &root, Json &settings, std::set<std::string> &carried)
{
	auto copyAs = [&](const char *name, const char *key) {
		if (const Json *v = objectAt(&root, name))
		{
			put(settings, key, Json::string(json::write(*v)));
			carried.insert(name);
		}
	};
	/* hotkeys by action, as the window's own are kept */
	copyAs("HotkeyBindings", "hotkeys.saved");
	/* a machine's buttons, kept by Chimera by its controller's name and by
	 * the window by its core's: each core takes its controller's the first
	 * time it opens (ControlsViewModel) */
	{
		Json from = object();
		for (const char *name : {"AllTrollers", "AllTrollersAutoFire", "AllTrollersAnalog"})
			if (const Json *v = objectAt(&root, name))
			{
				from.members.emplace_back(name, *v);
				carried.insert(name);
			}
		if (!from.members.empty()) put(settings, "controls.fromChimera", Json::string(json::write(from)));
	}
	/* View > Window Size's scale of each system */
	copyAs("TargetZoomFactors", "display.windowScales");
	/* the paths: the global ones, and each system's */
	if (const Json *entries = objectAt(&root, "PathEntries"))
	{
		const Json *paths = entries->find("Paths");
		if (paths != nullptr && paths->kind == Json::Kind::Array)
		{
			Json changed = object(), systems, systemChanged = object();
			systems.kind = Json::Kind::Array;
			std::string base;
			for (const Json &p : paths->items)
			{
				const Json *type = p.find("Type"), *path = p.find("Path"), *system = p.find("System");
				if (type == nullptr || path == nullptr || system == nullptr || type->kind != Json::Kind::String ||
					path->kind != Json::Kind::String || system->kind != Json::Kind::String)
					continue;
				if (system->text == "Global_NULL")
				{
					if (type->text == "Base") base = path->text == "." ? std::string() : path->text;
					else put(changed, type->text, *path);
					continue;
				}
				Json *mine = nullptr;
				for (auto &[s, v] : systemChanged.members)
					if (s == system->text) mine = &v;
				if (mine == nullptr)
				{
					systems.items.push_back(*system);
					systemChanged.members.emplace_back(system->text, object());
					mine = &systemChanged.members.back().second;
				}
				put(*mine, type->text, *path);
			}
			put(settings, "paths.base", Json::string(base));
			put(settings, "paths.changed", Json::string(json::write(changed)));
			put(settings, "paths.systems", Json::string(json::write(systems)));
			put(settings, "paths.systemChanged", Json::string(json::write(systemChanged)));
		}
		if (const Json *v = entries->find("UseRecentForRoms"); v != nullptr && v->kind == Json::Kind::Bool) put(settings, "paths.recentForRoms", *v);
		if (const Json *v = entries->find("LastRomPath"); v != nullptr && v->kind == Json::Kind::String) put(settings, "paths.lastRomFolder", *v);
		carried.insert("PathEntries");
	}
	/* the recent files */
	for (const auto &[name, key] : std::initializer_list<std::pair<const char *, const char *>>{{"RecentProjects", "session.recentProjects"},
			 {"RecentLua", "luaConsole.recentScripts"}, {"RecentLuaSession", "luaConsole.recentSessions"},
			 {"RecentWatches", "ramWatch.recentFiles"}, {"RecentMacros", "pianoRoll.recentMacros"}})
	{
		std::string text;
		if (recentLines(root.find(name), text))
		{
			put(settings, key, Json::string(text));
			carried.insert(name);
		}
	}
	/* firmware chosen by hand, "core/id" by Chimera, {"core": {"id"}} by the window */
	if (const Json *chosen = objectAt(&root, "CoreFirmware"))
	{
		Json byCore = object();
		for (const auto &[name, file] : chosen->members)
		{
			const size_t slash = name.find('/');
			if (slash == std::string::npos || file.kind != Json::Kind::String) continue;
			Json *mine = nullptr;
			for (auto &[c, v] : byCore.members)
				if (c == name.substr(0, slash)) mine = &v;
			if (mine == nullptr)
			{
				byCore.members.emplace_back(name.substr(0, slash), object());
				mine = &byCore.members.back().second;
			}
			put(*mine, name.substr(slash + 1), file);
		}
		put(settings, "cores.chosenFirmware", Json::string(json::write(byCore)));
		carried.insert("CoreFirmware");
	}
	/* the main window's place and size */
	{
		const Json *at = root.find("MainWindowPosition"), *size = root.find("MainWindowSize");
		int x = 0, y = 0, w = 0, h = 0;
		if (at != nullptr && size != nullptr && at->kind == Json::Kind::String && size->kind == Json::Kind::String &&
			std::sscanf(at->text.c_str(), "%d , %d", &x, &y) == 2 && std::sscanf(size->text.c_str(), "%d , %d", &w, &h) == 2 && w > 0 && h > 0)
		{
			put(settings, "display.windowGeometry",
				Json::string(std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," + std::to_string(h)));
			carried.insert("MainWindowPosition");
			carried.insert("MainWindowSize");
		}
	}
	/* each tool window's options and place, by Chimera's class name */
	if (const Json *common = objectAt(&root, "CommonToolSettings"))
	{
		static const std::pair<const char *, const char *> tools[] = {{"TAStudio", "tastudio"}, {"RamWatch", "ramWatch"},
			{"RamSearch", "ramSearch"}, {"HexEditor", "hexEditor"}, {"LuaConsole", "luaConsole"}, {"GenericDebugger", "debugger"},
			{"LogWindow", "logWindow"}, {"MacroInputTool", "macroInput"}, {"SurfaceViewer", "surfaceViewer"}, {"TraceLogger", "traceLogger"}};
		Json all = object();
		for (const auto &[name, s] : common->members)
		{
			const std::string cls = name.substr(name.rfind('.') + 1);
			const char *tool = nullptr;
			for (const auto &[c, t] : tools)
				if (cls == c) tool = t;
			if (tool == nullptr || s.kind != Json::Kind::Object) continue;
			Json mine = object();
			auto flag = [&](const char *from, const char *to) {
				if (const Json *v = s.find(from); v != nullptr && v->kind == Json::Kind::Bool) mine.members.emplace_back(to, *v);
			};
			flag("SaveWindowPosition", "save");
			flag("TopMost", "top");
			flag("FloatingWindow", "float");
			const Json *save = s.find("SaveWindowPosition"), *x = s.find("_wndx"), *y = s.find("_wndy"), *w = s.find("Width"),
				*h = s.find("Height");
			if (save != nullptr && save->boolean && x != nullptr && y != nullptr && w != nullptr && h != nullptr &&
				x->kind == Json::Kind::Number && y->kind == Json::Kind::Number && w->kind == Json::Kind::Number &&
				h->kind == Json::Kind::Number && whole(*w) > 0 && whole(*h) > 0)
				for (const auto &[k, v] : {std::pair<const char *, const Json *>{"x", x}, {"y", y}, {"w", w}, {"h", h}})
					mine.members.emplace_back(k, Json::integer(whole(*v)));
			all.members.emplace_back(tool, std::move(mine));
		}
		put(settings, "toolWindows.settings", Json::string(json::write(all)));
		carried.insert("CommonToolSettings");
	}
	/* the tools' own settings */
	const Json *custom = objectAt(&root, "CustomToolSettings");
	if (custom == nullptr) return;
	carried.insert("CustomToolSettings");
	auto settingsOf = [&](const char *cls) { return objectAt(objectAt(custom, (std::string("Chimera.Client.GUI.") + cls).c_str()), "Settings"); };
	/* a value of one tool's that is the window's as it is */
	auto same = [&](const Json *from, const char *name, const char *key, Json::Kind kind) {
		if (const Json *v = from != nullptr ? from->find(name) : nullptr; v != nullptr && v->kind == kind) put(settings, key, *v);
	};
	using K = Json::Kind;
	if (const Json *tas = settingsOf("TAStudio"))
	{
		for (const auto &[name, key] : std::initializer_list<std::pair<const char *, const char *>>{{"FollowCursor", "pianoRoll.followPlayback"},
				 {"FollowCursorAlwaysScroll", "pianoRoll.followAlways"}, {"ScrollSync", "pianoRoll.scrollSync"},
				 {"EditInvisibleColumns", "pianoRoll.editHiddenColumns"}, {"DenoteStatesWithBGColor", "pianoRoll.denoteStatesColor"},
				 {"DenoteStatesWithIcons", "pianoRoll.denoteStatesIcons"}, {"DenoteMarkersWithBGColor", "pianoRoll.denoteMarkersColor"},
				 {"DenoteMarkersWithIcons", "pianoRoll.denoteMarkersIcons"}, {"CopyIncludesFrameNo", "pianoRoll.copyFrameNumbers"},
				 {"StatesForMarkers", "pianoRoll.markersKeepStates"}, {"BindMarkersToInput", "pianoRoll.bindMarkers"},
				 {"MoveWithMainWindow", "pianoRoll.moveWithMainWindow"}, {"AutoPause", "session.pauseAtEnd"},
				 {"AutoRestoreLastPosition", "session.autoRestore"}, {"AutosaveAsBackupFile", "session.autosaveAsBackup"},
				 {"BackupPerFileSave", "session.backupOnSave"}, {"LoadBranchOnDoubleClick", "branches.loadOnDoubleClick"},
				 {"OldControlSchemeForBranches", "branches.oldControlScheme"}})
			same(tas, name, key, K::Bool);
		for (const auto &[name, key] : std::initializer_list<std::pair<const char *, const char *>>{{"ScrollSpeed", "pianoRoll.wheelRows"},
				 {"MaxUndoSteps", "pianoRoll.maxUndoSteps"}, {"RewindStep", "session.rewindStep"}, {"RewindStepFast", "session.rewindStepFast"},
				 {"BranchScreenshotSide", "branches.previewSide"}})
			same(tas, name, key, K::Number);
		same(tas, "FollowCursorScrollMethod", "pianoRoll.followScroll", K::String);
		if (const Json *v = tas->find("AutosaveInterval"); v != nullptr && v->kind == K::Number)
			put(settings, "session.autosaveSeconds", Json::integer(std::llround(v->number() / 1000.0)));
		static const char *const paints[] = {"never", "autofire", "always"}, *const patterns[] = {"hold", "autofire", "custom"};
		if (const Json *v = tas->find("PatternPaintMode"); v != nullptr && v->kind == K::Number && whole(*v) >= 0 && whole(*v) < 3)
			put(settings, "pianoRoll.patternMode", Json::string(paints[whole(*v)]));
		if (const Json *v = tas->find("PatternSelection"); v != nullptr && v->kind == K::Number && whole(*v) >= 0 && whole(*v) < 3)
			put(settings, "pianoRoll.patternKind", Json::string(patterns[whole(*v)]));
		if (const std::string f = font(tas->find("TasViewFont")); !f.empty()) put(settings, "pianoRoll.rollFont", Json::string(f));
		if (const Json *v = tas->find("SidebarWidth"); v != nullptr && v->kind == K::Number && whole(*v) > 0)
			put(settings, "pianoRoll.mainSplit", Json::integer(whole(*v)));
	}
	same(settingsOf("RamWatch"), "DoubleClickToPoke", "ramWatch.doubleClickToPoke", K::Bool);
	if (const Json *search = settingsOf("RamSearch"))
	{
		same(search, "PreviewMode", "ramSearch.previewMode", K::Bool);
		same(search, "AlwaysExcludeRamWatch", "ramSearch.alwaysExcludeRamWatch", K::Bool);
		same(search, "AutoSearchTakeLagFramesIntoAccount", "ramSearch.autoSearchAccountForLag", K::Bool);
		same(search, "UseUndoHistory", "ramSearch.useUndoHistory", K::Bool);
		if (std::string text; recentLines(search->find("RecentSearches"), text)) put(settings, "ramSearch.recentFiles", Json::string(text));
	}
	if (const Json *lua = settingsOf("LuaConsole"))
	{
		same(lua, "ReloadOnScriptFileChange", "luaConsole.reloadOnChange", K::Bool);
		same(lua, "ToggleAllIfNoneSelected", "luaConsole.toggleAllIfNoneSelected", K::Bool);
		same(lua, "DisableLuaScriptsOnLoad", "luaConsole.disableOnLoad", K::Bool);
		same(lua, "DuplicateToStdout", "luaConsole.duplicateToStdout", K::Bool);
	}
	/* the Hex Editor keeps its settings loose */
	if (const Json *hex = objectAt(custom, "Chimera.Client.GUI.HexEditor"))
	{
		same(hex, "LastDomain", "hexEditor.domain", K::String);
		same(hex, "BigEndian", "hexEditor.bigEndian", K::Bool);
		same(hex, "DataSize", "hexEditor.size", K::Number);
		if (std::string text; recentLines(hex->find("RecentTables"), text)) put(settings, "hexEditor.recentTables", Json::string(text));
		/* its own colours, when it was given them; else the theme's */
		const Json *customised = hex->find("ColorsCustomised");
		Json colors = object();
		if (const Json *c = objectAt(hex, "Colors"); c != nullptr && customised != nullptr && customised->boolean)
			for (const char *name : {"Background", "Foreground", "MenuBar", "Freeze", "Highlight", "HighlightFreeze"})
				if (const std::string hexColour = rgb(c->find(name)); !hexColour.empty()) colors.members.emplace_back(name, Json::string(hexColour));
		if (customised != nullptr) put(settings, "hexEditor.colors", Json::string(json::write(colors)));
	}
}
} // namespace

bool importChimeraSettings(const std::string &text, ChimeraImport &out, std::string &error)
{
	out = ChimeraImport();
	out.settings.kind = Json::Kind::Object;
	Json root;
	std::string why;
	if (!json::parse(text, root, why))
	{
		error = "not JSON (" + why + ")";
		return false;
	}
	if (!isChimeraSettings(root))
	{
		error = "not Chimera's settings";
		return false;
	}
	std::set<std::string> carried;
	/* the throttle Chimera picked, the resize size, the places: each made of several of Chimera's */
	std::string throttle;
	bool anyThrottle = false;
	int64_t resizeWidth = 0, resizeHeight = 0;
	bool anyResize = false;
	Json places;
	places.kind = Json::Kind::Object;
	for (const ChimeraSetting &s : chimeraSettings())
	{
		const Json *v = byName(root, s.name);
		if (v == nullptr) continue;
		bool took = true;
		switch (s.kind)
		{
		case Kind::Plain:
			if (isScalar(*v)) put(out.settings, s.key, *v);
			else took = false;
			break;
		case Kind::Choice:
		case Kind::Megabytes:
			if (v->kind != Json::Kind::Number) took = false;
			else if (s.kind == Kind::Choice) put(out.settings, s.key, Json::integer(whole(*v)));
			else put(out.settings, s.key, Json::integer(std::llround(v->number() / 1024.0)));
			break;
		case Kind::CoreKind:
			if (v->kind == Json::Kind::Number && (whole(*v) == 1 || whole(*v) == 2))
				put(out.settings, s.key, Json::string(whole(*v) == 1 ? "emulators" : "games"));
			else took = false;
			break;
		case Kind::Colour:
			if (v->kind == Json::Kind::Number)
			{
				char hex[12];
				std::snprintf(hex, sizeof hex, "#%08X", static_cast<unsigned>(static_cast<uint32_t>(whole(*v))));
				put(out.settings, s.key, Json::string(hex));
			}
			else took = false;
			break;
		case Kind::Throttle:
			if (v->kind == Json::Kind::Bool)
			{
				anyThrottle = true;
				if (v->boolean && throttle.empty()) throttle = s.extra;
			}
			else took = false;
			break;
		case Kind::Resize:
			if (v->kind == Json::Kind::Number)
			{
				anyResize = true;
				(std::string(s.extra) == "encoder.width" ? resizeWidth : resizeHeight) = whole(*v);
			}
			else took = false;
			break;
		case Kind::DisplayFeatures:
			if (v->kind == Json::Kind::Number)
			{
				put(out.settings, s.key, Json::boolean_(whole(*v) == 0));
				put(out.settings, s.extra, Json::boolean_(whole(*v) == 1));
			}
			else took = false;
			break;
		case Kind::NullWhenEmpty:
			if (v->kind == Json::Kind::Null) put(out.settings, s.key, Json::string(""));
			else if (v->kind == Json::Kind::String) put(out.settings, s.key, *v);
			else took = false;
			break;
		case Kind::Place:
		{
			const Json *x = v->find("X"), *y = v->find("Y"), *anchor = v->find("Anchor");
			if (x != nullptr && y != nullptr && anchor != nullptr && x->kind == Json::Kind::Number && y->kind == Json::Kind::Number &&
				anchor->kind == Json::Kind::Number)
			{
				Json place;
				place.kind = Json::Kind::Object;
				place.members.emplace_back("x", Json::integer(whole(*x)));
				place.members.emplace_back("y", Json::integer(whole(*y)));
				place.members.emplace_back("anchor", Json::integer(whole(*anchor)));
				places.members.emplace_back(s.extra, std::move(place));
			}
			else took = false;
			break;
		}
		}
		if (took) carried.insert(s.name);
	}
	if (anyThrottle) put(out.settings, "session.throttle", Json::string(throttle));
	if (anyResize)
	{
		const bool resize = resizeWidth > 0 && resizeHeight > 0;
		put(out.settings, "encoder.resize", Json::boolean_(resize));
		if (resize)
		{
			put(out.settings, "encoder.width", Json::integer(resizeWidth));
			put(out.settings, "encoder.height", Json::integer(resizeHeight));
		}
	}
	if (!places.members.empty()) put(out.settings, "messages.positions", Json::string(json::write(places)));
	carryRich(root, out.settings, carried);
	/* what Chimera has that was not carried, by its name; inside Movies by "Movies." and its own */
	for (const auto &[name, value] : root.members)
	{
		if (name == "Movies" && value.kind == Json::Kind::Object)
		{
			for (const auto &[inner, v] : value.members)
				(carried.count("Movies." + inner) != 0 ? out.carried : out.left).push_back("Movies." + inner);
			continue;
		}
		(carried.count(name) != 0 ? out.carried : out.left).push_back(name);
	}
	return true;
}

} // namespace chimera::vm
