#!/usr/bin/env python3
# Chimera's own command line, taken by the window that replaces it (review,
# 2026-10-26): --version; a project named with --project and its package with
# --core, opened and run; what is not a project and its package refused, as
# is Chimera's own settings file named with --config, left as it was;
# --chromeless and --luaconsole; and a --show name no window has, said.
#   command-line.py <chimera-qt> <work folder> <project> <package>
import json
import os
import shutil
import subprocess
import sys

qt, work, project, package = sys.argv[1:5]
shutil.rmtree(work, ignore_errors=True)
os.makedirs(work)
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", ALSOFT_DRIVERS="null",
           CHIMERA_QT_CONFIG=os.path.join(work, "settings.json"), CHIMERA_QT_CACHE=os.path.join(work, "cache"),
           CHIMERA_QT_RECOVERY=os.path.join(work, "recovery"), CHIMERA_QT_HASHES=os.path.join(work, "file-hashes.txt"),
           CHIMERA_QT_CHIMERA_SETTINGS=os.path.join(work, "no-chimera-config.ini"))
env.pop("DISPLAY", None)
failures = 0


def run(*args, seconds=60):
    try:
        return subprocess.run([qt] + list(args), env=env, timeout=seconds, capture_output=True, text=True)
    except subprocess.TimeoutExpired:
        return None


def check(ok, what, got=None):
    global failures
    print(("PASS  " if ok else "FAIL  ") + what + ("" if ok or got is None else "  (" + got + ")"))
    if not ok:
        failures += 1


r = run("--version")
check(r is not None and r.returncode == 0 and r.stdout.startswith("Commit ") and len(r.stdout.strip()) == len("Commit ") + 9,
      "--version prints the commit, as Chimera's", r and r.stdout.strip())

r = run("--project", project, "--core", package, "--play", "--measure", "1")
check(r is not None and r.returncode == 0 and "status: Opened gridWalker" in r.stdout,
      "--project and --core open the project and run it", r and (r.stdout + r.stderr)[-300:])

for refused, what in [([project, project, package], "three files"), ([project, os.path.join(work, "notes.txt")], "a package that is not one"),
                      ([os.path.join(work, "game.nes"), package], "a game and a package")]:
    r = run(*refused, "--measure", "1")
    check(r is not None and r.returncode == 2 and "a game opens only in a project" in r.stderr, "refused: " + what, r and r.stderr.strip())

chimera_config = os.path.join(work, "config.ini")
with open(chimera_config, "w") as f:
    json.dump({"LastWrittenFromDetailed": "a test", "SpeedPercent": 137}, f)
before = open(chimera_config).read()
r = run("--config", chimera_config, "--measure", "1")
check(r is not None and r.returncode == 2 and "Chimera's settings file" in r.stderr and open(chimera_config).read() == before
      and not os.path.exists(chimera_config + ".unreadable"), "--config naming Chimera's settings file is refused and left as it was",
      r and r.stderr.strip())

# the file named is the one read: one that does not read is set aside, as the window's own always is
own = os.path.join(work, "own.json")
with open(own, "w") as f:
    f.write("{ not json")
r = run("--config", own, "--measure", "0.5")
check(r is not None and r.returncode == 0 and os.path.exists(own + ".unreadable"), "--config names the settings file the window reads",
      r and r.stderr.strip())

shot = os.path.join(work, "chromeless.png")
r = run("--chromeless", "--luaconsole", "--measure", "0.5", "--screenshot", shot)
check(r is not None and r.returncode == 0 and "screenshot: luaConsoleWindow saved" in r.stdout,
      "--luaconsole opens the Lua Console", r and r.stdout.strip()[-300:])

r = run("--show", "no-such-window", "--measure", "0.5")
check(r is not None and r.returncode == 0 and "--show no-such-window: no tool window has that name" in r.stderr,
      "a --show name no window has is said", r and r.stderr.strip())

print("%s: %d failed" % ("FAIL" if failures else "PASS", failures))
sys.exit(1 if failures else 0)
