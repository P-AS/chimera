#!/usr/bin/env python3
# Chimera's settings carried over once (owner, 2026-10-26), in the window
# itself: started with no settings of its own and a Chimera config.ini named
# by CHIMERA_QT_CHIMERA_SETTINGS, the window keeps Chimera's settings as its
# own, and Lua's client.getconfig gives back what Chimera's file holds,
# setting by setting. Started again, the window keeps its own: the file is
# read once. The config.ini is made here, every setting the two share set to
# something that is not the window's default.
#   chimera-import.py <chimera-qt> <work folder>
import json
import math
import os
import shutil
import subprocess
import sys

qt, work = sys.argv[1], sys.argv[2]
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
    "InputHotkeyOverrideOptions": 2, "NoMixedInputHokeyOverride": True, "HotkeyConfigAutoTab": False, "InputConfigAutoTab": False,
    "DisplayWatchesOnScreen": False, "RamWatchDefinePrevious": 3,
    "CacheAutoClean": False, "CacheSizeLimitMb": 51200, "CacheFreeSpaceFloorMb": 10240,
    "CoresFolder": os.path.join(work, "cores"), "CoreManagerShows": 2, "FirmwareShows": 1, "NewProjectKind": 2,
    "DataDirectory": os.path.join(work, "data"), "DataDirectoryPending": None, "DataDirectoryPendingMove": False,
    "Theme": "Light", "DefaultAuthor": "Imported Author", "DisplaySubtitles": False,
    "CompileSessionGigabytes": 4, "SnowyNullCore": 0,
}
for i, name in enumerate(places):
    chimera[name] = {"X": 2 * i + 1, "Y": 2 * i + 2, "Anchor": i % 4}

shutil.rmtree(work, ignore_errors=True)
for folder in ("cores", "data"):
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
               CHIMERA_QT_CACHE=os.path.join(work, "cache"), CHIMERA_QT_RECOVERY=os.path.join(work, "recovery"),
               CHIMERA_QT_HASHES=os.path.join(work, "hashes.txt"))
    done = subprocess.run([qt, "--lua", os.path.join(here, "lua-config-dump.lua")], env=env, timeout=120,
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
# started again: the window's own settings, Chimera's file not read again
chimera["SpeedPercent"] = 99
said, again = run()
if again["config"].get("SpeedPercent") != 137:
    bad.append("started again, SpeedPercent is %r, not the 137 kept" % again["config"].get("SpeedPercent"))
if "carried over" in said:
    bad.append("started again, Chimera's settings were read again")
print("chimera import: %d settings compared, %d bad%s" % (compared, len(bad), "".join("\n  " + b for b in bad)))
sys.exit(1 if bad or compared == 0 else 0)
