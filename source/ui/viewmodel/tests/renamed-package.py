"""Writes a copy of a core package under another core name: a second
installed core that takes the same kind of file, for the test of choosing
between cores. With no package to copy it writes an empty file, and the test
that needs it skips."""
import json
import sys
import zipfile

src, name, out = sys.argv[1:4]
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
            data = json.dumps(cfg, indent=2).encode()
        zout.writestr(info, data)
