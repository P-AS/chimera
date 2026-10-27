"""The QML modules a bundle has to carry: each one Chimera's own QML imports,
and each one Qt's own dialogs import in the looks Chimera can give them
(Fusion's and the basic one), found in the bundle's qml folder. Chimera's own
modules (Chimera, ChimeraStyle, ChimeraDialogs) are built into the program
and are not looked for. stage-qt-linux.py and stage-qt-windows.py stop when
one is missing: a module left out fails only when a window that imports it
opens, as Qt's file dialog did without Qt.labs.folderlistmodel (issue 389)."""
import glob
import os
import re


def missing(qml, sources):
    """The modules missing from the bundle's qml folder, by name."""
    files = glob.glob(os.path.join(sources, "**", "*.qml"), recursive=True)
    dialogs = os.path.join(qml, "QtQuick", "Dialogs", "quickimpl", "qml")
    files += glob.glob(os.path.join(dialogs, "*.qml")) + glob.glob(os.path.join(dialogs, "+Fusion", "*.qml"))
    wanted = set()
    for f in files:
        with open(f, encoding="utf-8") as h:
            for m in re.findall(r"^import\s+([A-Za-z][\w.]*)", h.read(), re.M):
                if not m.startswith("Chimera"):
                    wanted.add(m)
    return sorted(m for m in wanted if not os.path.isfile(os.path.join(qml, *m.split("."), "qmldir")))
