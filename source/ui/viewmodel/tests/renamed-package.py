"""Writes a copy of a core package under another core name: a second
installed core that takes the same kind of file, for the test of choosing
between cores. With no package to copy it writes an empty file, and the test
that needs it skips."""
import json
import sys
import zipfile

src, name, out = sys.argv[1:4]
# optionally the core's kind ("game"), and firmware entries as JSON
kind = sys.argv[4] if len(sys.argv) > 4 else ""
firmware = json.loads(sys.argv[5]) if len(sys.argv) > 5 and sys.argv[5] else None
# and the date the package says it was made
version_date = sys.argv[6] if len(sys.argv) > 6 else ""
# and buttons more than the synthetic core's, as JSON (a second player,
# keyboard keys: for the piano roll's Columns menu)
more_buttons = json.loads(sys.argv[7]) if len(sys.argv) > 7 and sys.argv[7] else []
# and a core that compiles a game's code before a project is made ("precompile")
precompile = len(sys.argv) > 8 and sys.argv[8] == "precompile"
try:
    zin = zipfile.ZipFile(src)
except OSError:
    open(out, "wb").close()
    sys.exit(0)
with zin, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
    for info in zin.infolist():
        data = zin.read(info)
        if info.filename == "waterbox.config":
            cfg = json.loads(data)
            cfg["coreName"] = name
            if kind:
                cfg["kind"] = kind
            if firmware is not None:
                cfg["firmware"] = firmware
            if version_date:
                cfg["versionDate"] = version_date
            if more_buttons:
                cfg["input"]["buttons"] = cfg["input"]["buttons"] + more_buttons
            if precompile:
                cfg["precompile"] = True
            data = json.dumps(cfg, indent=2).encode()
        zout.writestr(info, data)
