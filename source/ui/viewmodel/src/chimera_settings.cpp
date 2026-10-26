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
