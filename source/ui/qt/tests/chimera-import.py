#!/usr/bin/env python3
# Chimera's settings carried over once (owner, 2026-10-26), in the window
# itself: started with no settings of its own and a Chimera config.ini named
# by CHIMERA_QT_CHIMERA_SETTINGS, the window keeps Chimera's settings as its
# own, and Lua's client.getconfig gives back what Chimera's file holds,
# setting by setting. Started again, the window keeps its own: the file is
# read once. The config.ini is made here, every setting the two share set to
# something that is not the window's default.
# The settings that are more than one value are checked too: each is kept by
# the window as carried over, and the synthetic core, opened, takes
# Chimera's bindings for its controller.
#   chimera-import.py <chimera-qt> <work folder> <synth package> <rom>
import json
import math
import os
import shutil
import subprocess
import sys

qt, work, package, rom = sys.argv[1:5]
here = os.path.dirname(os.path.abspath(__file__))

places = ["Fps", "FrameCounter", "LagCounter", "InputDisplay", "RamWatches", "Messages", "ReRecordCounter", "Autohold", "GameTime"]
chimera = {
    "LastWrittenFromDetailed": "a test",
    "HotkeyBindings": {"Pause": "P"},
    "JmdThreads": 3,
    "McpAllowChanges": True,
    "SpeedPercent": 137, "SpeedPercentAlternate": 250,
    "ClockThrottle": False, "SoundThrottle": False, "VSyncThrottle": True,
    "Unthrottled": True, "UnthrottledDrawOneIn": 5, "VSync": False, "TurboSeek": True, "SkipLagFrame": True,
    "AcceptBackgroundInput": True, "CaptureMouse": False, "ScreenshotCaptureOsd": True,
    "AutofireOn": 2, "AutofireOff": 3, "AutofireLagFrames": False,
    "DisplayFps": True, "DisplayAverageFps": True, "DisplayFrameCounter": True, "DisplayLagCounter": True,
    "DisplayInput": True, "DisplayRerecordCount": True, "DisplayGameTime": True, "DisplayMessages": False,
    "DispChromeStatusBarWindowed": False, "DispSpeedupFeatures": 1,
    "SoundEnabled": False, "SoundEnabledNormal": False, "SoundEnabledRWFF": False, "MuteFrameAdvance": False,
    "SoundVolume": 42, "SoundVolumeRWFF": 30, "SoundDevice": "", "SoundBufferSizeMs": 150,
    "Movies": {"GreenzoneBudgetMb": 2048, "GreenzoneMaxNearStride": 8, "GreenzonePictureSettleFrames": 5, "MovieEndAction": 1},
    "StartPaused": True, "StartFullscreen": False, "SuppressAskSave": True, "RunLuaDuringTurbo": False,
    "ShowContextMenu": False, "HandleAlternateKeyboardLayouts": True, "MergeLAndRModifierKeys": False,
    "SingleInstanceMode": False, "GCAdapterSupportEnabled": False,
    "StackOSDMessages": False, "OSDMessageDuration": 5,
    "MessagesColor": -16711936, "AlertMessageColor": -65281, "LastInputColor": -16776961, "MovieInputColor": -256,
    "DispFinalFilter": 1, "DispFixAspectRatio": False, "DispFixScaleInteger": True, "DispManagerAR": 3,
    "DispCustomUserARWidth": 320, "DispCustomUserARHeight": 240, "DispCustomUserArx": 16.0, "DispCustomUserAry": 9.0,
    "DispCropLeft": 1, "DispCropTop": 2, "DispCropRight": 3, "DispCropBottom": 4,
    "DispPrescale": 2, "DispAutoPrescale": True, "DispFullscreenHacks": True, "DispChromeFrameWindowed": 1,
    "DispChromeCaptionWindowed": False, "DispChromeMenuWindowed": False, "DispChromeStatusBarFullscreen": True,
    "DispChromeMenuFullscreen": True, "DispChromeFullscreenAutohideMouse": False, "DispChromeAllowDoubleClickFullscreen": False,
    "ScaleOSDWithSystemScale": False, "ResizeWithFramebuffer": False, "SaveWindowPosition": True,
    "MainFormStayOnTop": False, "MainFormMouseCaptureForcesTopmost": True, "UseStaticWindowTitles": True,
    "MainFormStaticWindowTitleOverride": "Imported",
    "AviCaptureOsd": True, "AviCaptureLua": True, "VideoWriterAudioSync": False, "AVWriterPad": True,
    "AVWriterResizeWidth": 640, "AVWriterResizeHeight": 480, "FFmpegCustomCommand": "-c:v libx264 -crf 18",
    "InputHotkeyOverrideOptions": 2, "NoMixedInputHokeyOverride": True, "HotkeyConfigAutoTab": False, "InputConfigAutoTab": False, "RelativeMouseSensitivity": 150,
    "DisplayWatchesOnScreen": False, "RamWatchDefinePrevious": 3,
    "CacheAutoClean": False, "CacheSizeLimitMb": 51200, "CacheFreeSpaceFloorMb": 10240,
    "CoresFolder": os.path.join(work, "cores"), "CoreManagerShows": 2, "FirmwareShows": 1, "NewProjectKind": 2,
    "DataDirectory": os.path.join(work, "data"), "DataDirectoryPending": None, "DataDirectoryPendingMove": False,
    "Theme": "Light", "DefaultAuthor": "Imported Author", "DisplaySubtitles": False,
    "CompileSessionGigabytes": 4, "SnowyNullCore": 0,
}
for i, name in enumerate(places):
    chimera[name] = {"X": 2 * i + 1, "Y": 2 * i + 2, "Anchor": i % 4}
here_files = os.path.join(work, "files")
chimera.update({
    "HotkeyBindings": {"Pause": "P", "Frame Advance": "F"},
    "AllTrollers": {"Synth Controller": {"P1 A": "K"}, "Other Controller": {"P1 A": "Q"}},
    "AllTrollersAutoFire": {"Synth Controller": {"P1 B": "J"}},
    "TargetZoomFactors": {"NULL": 3},
    "PathEntries": {"Paths": [{"Type": "Base", "Path": ".", "System": "Global_NULL"},
                              {"Type": "Movies", "Path": "./Runs", "System": "Global_NULL"},
                              {"Type": "Screenshots", "Path": "./Shots", "System": "NES"}],
                    "UseRecentForRoms": False, "LastRomPath": here_files},
    "RecentProjects": {"recentlist": [os.path.join(here_files, "b.chimeraProject")]},
    "RecentLua": {"recentlist": [os.path.join(here_files, "s.lua")]},
    "RecentLuaSession": {"recentlist": [os.path.join(here_files, "s.luases")]},
    "RecentWatches": {"recentlist": [os.path.join(here_files, "w.wch")]},
    "RecentMacros": {"recentlist": [os.path.join(here_files, "m.bk2m")]},
    "CoreFirmware": {"SDLPoP/PRINCE.DAT": os.path.join(here_files, "PRINCE.DAT")},
    "MainWindowPosition": "40, 50", "MainWindowSize": "700, 500",
    "CommonToolSettings": {"Chimera.Client.GUI.TAStudio": {"_wndx": 10, "_wndy": 20, "Width": 300, "Height": 400,
                                                         "SaveWindowPosition": True, "TopMost": False, "FloatingWindow": True}},
    "CustomToolSettings": {
        "Chimera.Client.GUI.TAStudio": {"Settings": {
            "FollowCursor": False, "FollowCursorAlwaysScroll": True, "ScrollSync": False, "EditInvisibleColumns": False,
            "DenoteStatesWithBGColor": False, "DenoteStatesWithIcons": True, "DenoteMarkersWithBGColor": False,
            "DenoteMarkersWithIcons": True, "CopyIncludesFrameNo": True, "StatesForMarkers": False, "BindMarkersToInput": True,
            "MoveWithMainWindow": False, "AutoPause": True, "AutoRestoreLastPosition": False, "AutosaveAsBackupFile": True,
            "BackupPerFileSave": True, "LoadBranchOnDoubleClick": False, "OldControlSchemeForBranches": True,
            "ScrollSpeed": 3, "MaxUndoSteps": 500, "RewindStep": 2, "RewindStepFast": 8, "BranchScreenshotSide": 200,
            "FollowCursorScrollMethod": "center", "AutosaveInterval": 120000, "PatternPaintMode": 2, "PatternSelection": 1,
            "TasViewFont": "Arial, 9pt, style=Bold", "SidebarWidth": 300}},
        "Chimera.Client.GUI.RamWatch": {"Settings": {"DoubleClickToPoke": False}},
        "Chimera.Client.GUI.RamSearch": {"Settings": {"PreviewMode": False, "AlwaysExcludeRamWatch": True,
                                                      "AutoSearchTakeLagFramesIntoAccount": False, "UseUndoHistory": False,
                                                      "RecentSearches": {"recentlist": [os.path.join(here_files, "r.wch")]}}},
        "Chimera.Client.GUI.LuaConsole": {"Settings": {"ReloadOnScriptFileChange": True, "ToggleAllIfNoneSelected": False,
                                                       "DisableLuaScriptsOnLoad": False, "DuplicateToStdout": False}},
        "Chimera.Client.GUI.HexEditor": {"LastDomain": None, "BigEndian": True, "DataSize": 2, "ColorsCustomised": True,
                                         "RecentTables": {"recentlist": [os.path.join(here_files, "t.tbl")]},
                                         "Colors": {"Background": "30, 30, 30", "Foreground": "224, 224, 224"}}},
})
# the window's settings the ones above become, each to be kept as carried
rich = ["hotkeys.saved", "controls.fromChimera", "display.windowScales", "paths.base", "paths.changed", "paths.systems",
        "paths.systemChanged", "paths.recentForRoms", "paths.lastRomFolder", "session.recentProjects", "luaConsole.recentScripts",
        "luaConsole.recentSessions", "ramWatch.recentFiles", "pianoRoll.recentMacros", "cores.chosenFirmware",
        "display.windowGeometry", "toolWindows.settings", "pianoRoll.followPlayback", "pianoRoll.followAlways",
        "pianoRoll.scrollSync", "pianoRoll.editHiddenColumns", "pianoRoll.denoteStatesColor", "pianoRoll.denoteStatesIcons",
        "pianoRoll.denoteMarkersColor", "pianoRoll.denoteMarkersIcons", "pianoRoll.copyFrameNumbers", "pianoRoll.markersKeepStates",
        "pianoRoll.bindMarkers", "pianoRoll.moveWithMainWindow", "session.pauseAtEnd", "session.autoRestore",
        "session.autosaveAsBackup", "session.backupOnSave", "branches.loadOnDoubleClick", "branches.oldControlScheme",
        "pianoRoll.wheelRows", "pianoRoll.maxUndoSteps", "session.rewindStep", "session.rewindStepFast", "branches.previewSide",
        "pianoRoll.followScroll", "session.autosaveSeconds", "pianoRoll.patternMode", "pianoRoll.patternKind", "pianoRoll.rollFont",
        "pianoRoll.mainSplit", "ramWatch.doubleClickToPoke", "ramSearch.previewMode", "ramSearch.alwaysExcludeRamWatch",
        "ramSearch.autoSearchAccountForLag", "ramSearch.useUndoHistory", "ramSearch.recentFiles", "luaConsole.reloadOnChange",
        "luaConsole.toggleAllIfNoneSelected", "luaConsole.disableOnLoad", "luaConsole.duplicateToStdout", "hexEditor.bigEndian",
        "hexEditor.size", "hexEditor.recentTables", "hexEditor.colors", "controls.saved", "controls.savedAutofire"]

shutil.rmtree(work, ignore_errors=True)
for folder in ("cores", "data", "files"):
    os.makedirs(os.path.join(work, folder))
config = os.path.join(work, "config.ini")
settings = os.path.join(work, "settings.json")
dump = os.path.join(work, "dump.json")


def run():
    with open(config, "w", encoding="utf-8") as f:
        json.dump(chimera, f, indent=2)
    if os.path.exists(dump):
        os.remove(dump)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", ALSOFT_DRIVERS="null",
               CHIMERA_QT_CONFIG=settings, CHIMERA_QT_CHIMERA_SETTINGS=config, CHIMERA_TEST_DUMP=dump,
               CHIMERA_TEST_WAIT_OPEN="1", CHIMERA_TEST_KEYS=",".join(rich),
               CHIMERA_QT_CACHE=os.path.join(work, "cache"), CHIMERA_QT_RECOVERY=os.path.join(work, "recovery"),
               CHIMERA_QT_HASHES=os.path.join(work, "hashes.txt"))
    # a game runs only in a project (owner, 2026-10-26)
    project = os.path.join(work, "files", "game.chimeraProject")
    subprocess.run([sys.executable, os.path.join(here, "game-project.py"), package, rom, project], check=True)
    done = subprocess.run([qt, project, package, "--lua", os.path.join(here, "lua-config-dump.lua")], env=env, timeout=120,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if done.returncode != 0 or not os.path.exists(dump):
        print(done.stdout[-3000:])
        sys.exit("the window ended with %d, %s" % (done.returncode, "a dump" if os.path.exists(dump) else "no dump"))
    with open(dump, encoding="utf-8") as f:
        return done.stdout, json.load(f)


def expected(kind, extra, value, name):
    if kind == "choice":
        return extra.split("|")[value]
    if kind == "corekind":
        return {1: "Emulators", 2: "Games"}[value]
    if kind == "megabytes":
        return round(value / 1024) * 1024
    if kind == "resize":
        return value if chimera["AVWriterResizeWidth"] > 0 and chimera["AVWriterResizeHeight"] > 0 else 0
    if kind == "nullwhenempty":
        return value or None
    if kind == "place":
        return {"X": value["X"], "Y": value["Y"], "Anchor": ["TopLeft", "TopRight", "BottomLeft", "BottomRight"][value["Anchor"]]}
    return value


bad = []
said, got = run()
names = [line.split("\t") for line in got["names"].splitlines()]
compared = 0
for name, kind, key, extra in names:
    outer, _, inner = name.partition(".")
    source = chimera.get(outer, {}).get(inner) if inner else chimera.get(name)
    have = got["config"].get(outer, {}).get(inner) if inner else got["config"].get(name)
    if source is None and kind != "nullwhenempty":
        bad.append(name + " is not in the test's config.ini")
        continue
    want = expected(kind, extra, source, name)
    same = math.isclose(have, want) if isinstance(want, float) and isinstance(have, (int, float)) else have == want
    if not same:
        bad.append("%s: %r, not %r" % (name, have, want))
    compared += 1
with open(settings, encoding="utf-8") as f:
    kept = json.load(f)
if kept.get("importedFrom") != config:
    bad.append("the settings do not say where they came from: %r" % kept.get("importedFrom"))
if "mcp.allowChanges" in kept:
    bad.append("the MCP server's permission was carried over")
if "of Chimera's carried over from" not in said:
    bad.append("the start did not say it carried Chimera's settings over")


def same_value(a, b):
    """equal, or the same JSON written another way"""
    if a == b:
        return True
    try:
        return isinstance(a, str) and isinstance(b, str) and json.loads(a) == json.loads(b)
    except ValueError:
        return False


# each kept by the window as carried: the window writes its settings as they
# change, so one refused would be left in the file as carried, the window
# holding another
def placeless(text):
    """TAStudio opens beside the main window with the project and keeps only
    its size: where it was when it closed is written, and not used"""
    try:
        value = json.loads(text)
        for name in ("x", "y"):
            value.get("tastudio", {}).pop(name, None)
        return json.dumps(value, sort_keys=True)
    except (TypeError, ValueError, AttributeError):
        return text


for key in rich:
    if key not in kept:
        bad.append("%s was not carried over" % key)
    elif key == "toolWindows.settings" and placeless(got["keys"].get(key)) == placeless(kept[key]):
        pass
    elif not same_value(got["keys"].get(key), kept[key]):
        bad.append("%s: the window holds %r, the file %r" % (key, got["keys"].get(key), kept[key]))
keys = got["keys"]
# a few by what they must be
if json.loads(keys.get("hotkeys.saved") or "{}").get("Pause") != "P":
    bad.append("the Pause hotkey is not P")
if json.loads(keys.get("controls.saved") or "{}").get("Synth", {}).get("P1 A") != "K":
    bad.append("the synthetic core did not take Chimera's P1 A: %r" % keys.get("controls.saved"))
if json.loads(keys.get("controls.savedAutofire") or "{}").get("Synth", {}).get("P1 B") != "J":
    bad.append("the synthetic core did not take Chimera's autofire P1 B: %r" % keys.get("controls.savedAutofire"))
if json.loads(keys.get("toolWindows.settings") or "{}").get("tastudio") != {"save": True, "top": False, "float": True, "x": 10, "y": 20, "w": 300, "h": 400}:
    bad.append("TAStudio's window is not as Chimera left it: %r" % keys.get("toolWindows.settings"))
if json.loads(keys.get("pianoRoll.rollFont") or "{}") != {"family": "Arial", "pointSize": 9, "bold": True, "italic": False}:
    bad.append("the roll's font is %r" % keys.get("pianoRoll.rollFont"))
if keys.get("session.autosaveSeconds") != 120 or keys.get("pianoRoll.patternMode") != "always":
    bad.append("TAStudio's autosave or paint pattern is wrong")
# started again: the window's own settings, Chimera's file not read again
chimera["SpeedPercent"] = 99
said, again = run()
if again["config"].get("SpeedPercent") != 137:
    bad.append("started again, SpeedPercent is %r, not the 137 kept" % again["config"].get("SpeedPercent"))
if "carried over" in said:
    bad.append("started again, Chimera's settings were read again")
print("chimera import: %d settings compared, %d more kept as carried, %d bad%s" % (compared, len(rich), len(bad),
      "".join("\n  " + b for b in bad)))
sys.exit(1 if bad or compared == 0 else 0)
