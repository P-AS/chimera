#!/usr/bin/env python3
"""Puts the Qt window's Windows build in one folder that runs on its own: the
program, Chimera's DLLs, the QML modules the window imports, and every DLL
those import (from Qt or the compiler), found by reading their import tables.
Everything is stripped of debug information, as it would ship.

    tools/stage-qt-windows.py <meson build dir> <installed dll dir> <windows qt> <out>

Prints the folder's size and Qt's share of it."""
import os
import shutil
import subprocess
import sys

build, dlls, qt, out = sys.argv[1:5]
runtime = ["/usr/lib/gcc/x86_64-w64-mingw32/13-posix", "/usr/x86_64-w64-mingw32/lib"]

if os.path.exists(out):
    sys.exit(f"{out} exists: give a new folder")
os.makedirs(out)
for exe in ("chimera-qt.exe", "test_vm.exe"):
    shutil.copy2(f"{build}/source/ui/{exe}", out)
for d in ("libchimera.dll", "libminiboxhost.dll", "libzstd.dll", "lua54.dll", "SDL2.dll", "OpenAL32.dll",
          "libchimerahash.dll", "chimera_crash.dll"):
    shutil.copy2(f"{dlls}/{d}", out)
os.makedirs(f"{out}/platforms")
shutil.copy2(f"{qt}/plugins/platforms/qwindows.dll", f"{out}/platforms")

# QML: QtQml, QtCore and the QtQuick modules the window imports. Of the
# control styles only Fusion (the one used) and Basic (every style's fallback).
quick = {"Controls", "Templates", "Layouts", "Dialogs", "Window"}
styles = {"Fusion", "Basic", "impl"}
def ignore(dirpath, names):
    rel = os.path.relpath(dirpath, f"{qt}/qml")
    if rel == "QtQuick":
        return [n for n in names if os.path.isdir(os.path.join(dirpath, n)) and n not in quick]
    if rel == "QtQuick/Controls":
        return [n for n in names if os.path.isdir(os.path.join(dirpath, n)) and n not in styles]
    return [n for n in names if n.endswith(".debug")]
for m in ("QtQml", "QtCore", "QtQuick"):
    shutil.copytree(f"{qt}/qml/{m}", f"{out}/qml/{m}", ignore=ignore)

def imports(path):
    o = subprocess.run(["x86_64-w64-mingw32-objdump", "-p", path], capture_output=True, text=True, check=True).stdout
    return [line.split()[-1] for line in o.splitlines() if "DLL Name:" in line]

have = {f.lower() for f in os.listdir(out)}
todo = [os.path.join(r, f) for r, _, fs in os.walk(out) for f in fs if f.lower().endswith((".dll", ".exe"))]
while todo:
    for dll in imports(todo.pop()):
        if dll.lower() in have:
            continue
        for d in [f"{qt}/bin"] + runtime:
            if os.path.exists(f"{d}/{dll}"):
                shutil.copy2(f"{d}/{dll}", out)
                have.add(dll.lower())
                todo.append(os.path.join(out, dll))
                break

for r, _, fs in os.walk(out):
    for f in fs:
        if f.lower().endswith((".dll", ".exe")):
            subprocess.run(["x86_64-w64-mingw32-strip", "--strip-unneeded", os.path.join(r, f)], check=True)

def size(pred):
    return sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(out) for f in fs if pred(os.path.join(r, f)))
total = size(lambda p: True)
qtpart = size(lambda p: os.path.basename(p).startswith("Qt6") or f"{os.sep}qml{os.sep}" in p or f"{os.sep}platforms{os.sep}" in p)
print(f"{out}: {total / 1e6:.1f} MB, of which Qt {qtpart / 1e6:.1f} MB")
print("DLLs:", " ".join(sorted(f for f in os.listdir(out) if f.lower().endswith(".dll"))))
