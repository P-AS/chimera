"""Writes a copy of the synthetic core package as "Synth Slots": a package of
two machines (boards) with declared file slots, for the New Project tests.
Board A takes the synthetic game as a cartridge; board B takes disks (a cue
sheet or an image named like a disk) and needs a firmware file. At least
one of the two kinds of file is needed. It has an analog control, a dial.
With no package to copy it writes an empty file, and the tests that need it
skip."""
import json
import sys
import zipfile

src, out = sys.argv[1:3]
try:
    zin = zipfile.ZipFile(src)
except OSError:
    open(out, "wb").close()
    sys.exit(0)
slots = {
    "slots": [
        {"id": "rom", "title": "Cartridge", "min": 0, "max": 1, "formats": ["testrom"],
         "help": "The synthetic game.", "exposedWhen": {"setting": "board", "is": "a"}},
        {"id": "disk", "title": "Disks", "min": 0, "max": -1, "formats": ["cue", "img"],
         "help": "Disks, in the order they are swapped.", "namePattern": "^disk[0-9]*\\.(cue|img)$",
         "nameHelp": "A disk is called disk, a number and .cue or .img."},
    ],
    "atLeastOneOf": [["rom", "disk"]],
}
with zin, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
    for info in zin.infolist():
        data = zin.read(info)
        if info.filename == "waterbox.config":
            cfg = json.loads(data)
            cfg["coreName"] = "Synth Slots"
            cfg["machineSetting"] = "board"
            cfg["settings"].append({"name": "board", "display": "Board", "type": "enum",
                                    "options": ["a", "b"], "default": "a", "sync": True,
                                    "description": "Which board the project is."})
            cfg["settings"].append({"name": "diskSpeed", "display": "Disk Speed", "type": "int",
                                    "min": 1, "max": 4, "default": 1, "sync": True, "when": ["b"],
                                    "description": "Board B's alone."})
            cfg["machines"] = [
                {"id": "Synth", "label": "Board A", "when": ["a"]},
                {"id": "SynthB", "label": "Board B", "when": ["b"]},
            ]
            cfg["firmware"] = [{"id": "bios", "display": "Board B BIOS", "name": "boardb.bin",
                                "requiredWhen": {"setting": "board", "is": "b"}}]
            # presets: one for every board (with a value for no setting, and the
            # board, which is not a preset's to move), one for board B alone
            cfg["presets"] = [
                {"id": "full", "label": "Full fill", "description": "fills RAM with 255",
                 "values": {"initFillByte": 255, "board": "b", "nope": 1}},
                {"id": "fast", "label": "Fast disks", "when": ["b"], "values": {"diskSpeed": 4}},
            ]
            # the synthetic core suggests settings for a game (SuggestSettings)
            cfg["suggestSettings"] = True
            # the synthetic core's importer: a movie of its own lines, the game it
            # was made on, and a setting the movie dictates
            cfg["movieImport"] = {
                "menu": "Import Synth Movie...",
                "movie": {"label": "A synthetic movie", "extensions": ["txt"]},
                "files": [{"option": "importRom", "label": "The game", "extensions": ["testrom"],
                           "slot": "rom", "required": True}],
                "options": [{"name": "importFill", "display": "Fill RAM", "type": "bool"}],
            }
            # an analog control, for the piano roll's analog columns
            cfg["input"]["axes"] = [{"name": "P1 Dial", "min": -100, "max": 100, "neutral": 0, "header": "Dl"}]
            data = json.dumps(cfg, indent=2).encode()
        zout.writestr(info, data)
    zout.writestr("file_slots.json", json.dumps(slots, indent=2))
