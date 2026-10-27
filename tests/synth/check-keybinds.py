#!/usr/bin/env python3
# Asserts that the bindings a core package ships are the machine's when nobody changed them.
#
# Usage: check-keybinds.py <the machine's bindings (dump-controls.lua)> <package default_keybinds.json> <controller>
import json
import sys

cfg = json.load(open(sys.argv[1]))
shipped = json.load(open(sys.argv[2]))
name = sys.argv[3]

want = shipped["AllTrollers"][name]
# An empty set of shipped bindings agrees with ANY config: the comparison below
# is over want's own keys, so with nothing to want, nothing can differ and the
# check passes having compared nothing. That is how a gate stays green over an
# empty database, so say it instead.
if not want:
    sys.exit(f"the package ships no bindings for '{name}' - there is nothing to check")
got = cfg.get("AllTrollers", {}).get(name)
if got is None:
    sys.exit(f"the frontend has no bindings at all for '{name}'")
differing = {k: (v, got.get(k)) for k, v in want.items() if got.get(k) != v}
if differing:
    sys.exit(f"bindings differ from the package's: {differing}")

wantAnalog = shipped.get("AllTrollersAnalog", {}).get(name)
nAnalog = 0
if wantAnalog is not None:
    gotAnalog = cfg.get("AllTrollersAnalog", {}).get(name)
    if gotAnalog is None:
        sys.exit(f"the frontend has no analog bindings for '{name}'")
    for axis, bind in wantAnalog.items():
        gb = gotAnalog.get(axis)
        if gb is None:
            sys.exit(f"analog bind for '{axis}' was not adopted")
        for field, v in bind.items():
            # no key is "" in a package and null in the window: the same
            if gb.get(field) != v and not (gb.get(field) in (None, "") and v in (None, "")):
                sys.exit(f"analog bind '{axis}.{field}' differs: shipped {v!r}, config {gb.get(field)!r}")
    nAnalog = len(wantAnalog)

wantAF = shipped.get("AllTrollersAutoFire", {}).get(name)
if wantAF is not None:
    gotAF = cfg.get("AllTrollersAutoFire", {}).get(name)
    if gotAF != wantAF:
        sys.exit(f"autofire bindings differ from the package's: shipped {wantAF}, config {gotAF}")
print(f"{name}: {len(want)} bindings and {nAnalog} analog bindings adopted from the package")
