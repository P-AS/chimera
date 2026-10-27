"""Runs the window for a test in a folder of its own, emptied first: its
settings, its caches and the unsaved work it keeps against a crash. A run
killed before would leave its work there to be offered back at the next
open, and the window would wait on the question. The window is killed when
it outlives the time given, so none is left running.

  window-run.py <folder> <seconds> <chimera> [arguments...]
"""
import os
import shutil
import subprocess
import sys

folder, seconds, qt, rest = sys.argv[1], float(sys.argv[2]), sys.argv[3], sys.argv[4:]
shutil.rmtree(folder, ignore_errors=True)
os.makedirs(folder)
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", ALSOFT_DRIVERS="null",
           CHIMERA_QT_CONFIG=os.path.join(folder, "settings.json"), CHIMERA_QT_CACHE=os.path.join(folder, "cache"),
           CHIMERA_QT_RECOVERY=os.path.join(folder, "recovery"), CHIMERA_QT_HASHES=os.path.join(folder, "file-hashes.txt"))
try:
    sys.exit(subprocess.run([qt] + rest, env=env, timeout=seconds).returncode)
except subprocess.TimeoutExpired:
    sys.exit("the window was still running after %g seconds" % seconds)
