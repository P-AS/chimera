# A Qt interface for Chimera (issue 142)

Private for now (owner, 2026-10-22). This is the prototype of a new
interface in C++ only, to replace the Windows Forms one and with it C#. The
code is in `source/ui`; how it is built and run is in
[source/ui/README.md](../source/ui/README.md).

## What was decided

- Qt 6.12 (a long-term-support release), with the windows written in Qt
  Quick (QML) (owner, 2026-10-22).
- The logic behind each window lives in C++ "view models" with no Qt in
  them. A window only lays things out and binds to them (owner,
  2026-10-22).
- **The engine owns the movie** (owner, 2026-10-22). The window reads
  and edits a frame's input through the engine (`ce_session_movie_get`,
  `_set`, `_resize`, `_text`), which writes every line in the movie's own
  columns; the window never writes one. Those calls may come from any
  thread while another runs the machine: the movie has a lock of its own.
  An edit is honoured before the machine next moves: the engine drops the
  states after the earliest frame edited and, when the machine is already
  past it, goes back before it and plays the movie up to where it was. The
  window plays and seeks with the engine's own calls
  (`ce_session_movie_advance`, `ce_session_seek`). The other ways - an
  export of the engine's line writer to the window, or a copy of the
  format in the window as the C# one has - were set aside. One more call
  follows from it: `ce_session_movie_at` says that a state the window
  loaded itself (a branch's) put the machine on a frame of the movie, so
  the engine counts from there instead of replaying to it. And the rule
  for which machine a project makes (core build, settings, firmware,
  files), which decides whether kept states may be loaded, moved from the
  C# into the engine as `ce_project_machine_id`, and so did rerecord
  counting (`ce_session_rerecords`): an edit counts one when it drops a
  stored state, a stroke counts once, a branch loaded over the movie
  counts one, as Chimera's TAStudio counts them.
- Slint was considered and set aside for now. It is the closest fit to
  this pattern, but it has no docking, fewer desktop controls, and its
  licence is GPLv3 or an attribution licence, against Chimera's MIT.

## What the prototype does

Each window binds to view models through one generic adapter that knows
none of them. Lua scripts reach the same view models by the same names;
the list of names (`--describe`) is what an MCP server's tools would be
made from.

| View model | What it does | Window |
|---|---|---|
| session | open a game (the installed core that takes its kind of file opens it; when several do, they are offered), open, save and make projects, recent projects, play, pause, frame advance, restart, seek, keys and gamepads, sound, the greenzone (kept with the project in its cache folder and loaded when the project opens, unless the last session did not end), settings applied live or by restarting | main window |
| pianoRoll | a row a frame and a column a button; press, drag, paint, fill; undo and redo (a stroke is one step); markers; follows the current frame; greenzone, lag and marker colours | main window (a C++ item draws it) |
| branches | other versions of the movie, kept in the project, each with its state in a file in the project's cache folder and the picture it had; loading one loads that state, or replays from where the two movies differ when the state is missing or is another machine's | Branches |
| ramSearch | the engine's RAM Search, updated after every frame, with undo; values shown and typed every way RAM Watch shows them | RAM Search (Qt's own table) |
| ramWatch | addresses kept an eye on, read after every frame, shown unsigned, signed, in hex, in binary, as fixed point or as a float exactly as Chimera writes them; the previous value three ways, changes counted; Chimera's .wch files both ways (other emulators' too); the marked ones shown over the picture where Chimera shows them | RAM Watch |
| settings | the package's declared settings, those that apply, edits applied live or by restarting | Settings (a form generated from the declaration) |
| luaConsole | edit, run, open and save scripts; their output | Lua Console |
| newProject | a project made before any machine runs, as Chimera's New Project window makes one: the core among the installed ones, its machine, files in the slots its package declares (kinds, how many, the ones a choice rules out, at least one of a group, names a slot asks for, a cue's tracks checked at once), the settings that apply, the firmware found by the engine's rules or chosen by hand; what is missing said a line at a time; made, then opened | New Project |
| cores | the installed core packages and the files each takes, the cores and firmware folders; a project's core is found here, and its firmware by the engine's rules | Cores |

The tests drive the view models with no window: 515 checks on the
synthetic core (and on two packages made from it at build time: one under
another name, one of two machines with declared slots and a firmware). They include a project whose movie ends with the machine's
memory byte for byte as the witness recorded it. One more test loads the
whole window off-screen and runs it for a second. Every new check was
broken on purpose once and seen to fail.

## What was measured

Linux, headless, Qt's software drawing:

- **Full speed while the piano roll scrolls.** Snes9x, Genesis Plus GX
  and the synthetic core ran at their own rate (60 frames a second) while
  the piano roll scrolled through 1,000,000 frames. Each redraw took
  0.24 to 0.52 ms.
- **Running without the picture:** about 3,850 frames a second (Genesis
  Plus GX) and 3,950 (Snes9x).
- **Seeking:**
  - jumping back 1,500 frames, to a stored state: 9 to 10 ms;
  - an edit 2,900 frames back, replayed to where the machine was: 0.7 s.

Windows (the GTX 1060, through WSL, screen locked):

- **Tests and speed:**
  - the view-model tests pass natively;
  - Qt chose Direct3D 11 on the GTX 1060;
  - the machine held 59.9 frames a second while the piano roll scrolled.
- **Download size:** 69 MB with debug information stripped, of which Qt
  is 57 MB.
- **Not measured yet:** the window on screen on the GPU. Windows lets no
  Direct3D window present frames while the screen is locked.

## What went wrong, and what was done

- **Qt's threading variant on Windows.** Qt built for Windows with the
  compiler's "win32" threading variant (the one the rest of Chimera's
  Windows build uses) crashed in `libstdc++-6.dll` as a Qt thread ended at
  exit. Built with the "posix" variant, as Qt supports, the crash is gone.
  The Qt window's Windows build uses its own cross file
  (`extern/meson/mingw-w64-qt.ini`).
- **OPEN: the exit hang.** On Windows, a program that has shown a Qt
  Quick window does not finish exiting; it stays listed, holding memory,
  and cannot be stopped. A minimal Qt Quick program built the same way
  does too, while one with no window exits cleanly. To be retried with
  the screen unlocked, and then against Qt's own prebuilt Windows
  libraries.
- **zstd in resources.** The Linux Qt's resource compiler packs with
  zstd, which the Windows Qt (built without it) cannot unpack:
  `--no-zstd`.
- **Network code.** The Linux Qt downloaded from Qt has its network
  module inside QML; the Windows one built here leaves it out, since
  Chimera has no network code. A shipped Linux build would need the same.
- **Caches in Chimera's folder.** Qt keeps caches under the application's
  name. Named "Chimera", the prototype wrote two cache files into the real
  Chimera's data folder; they were removed, and the prototype has a name
  of its own.
- **Frame numbers.** The engine counts frames only for a movie it plays
  itself, and a seek away and back within one report changes no frame
  number. The view models count their own frames, and Lua's per-frame
  hook runs on every report.
- **Lua and C++ destructors.** Lua reports errors by jumping out of the C
  function, past C++ destructors, so the bridge raises its errors only
  after its C++ objects have ended.
- **Two differences from Chimera's RAM Watch, on purpose.** A float is
  written as the shortest text that reads back as the same float, as .NET
  8 writes it (compared over 200,000 values); Chimera's C#, on .NET
  Framework, writes seven digits, so two different floats can look alike.
  And with "before the last change" chosen, the previous value is the one
  before the change; Chimera's C# sets it to the new value, so the column
  always equals the value.
- **Column names are not Qt's.** A table column called "display" took
  every cell's text, because Qt calls a cell's text "display". The adapter
  no longer lets a column name shadow one of Qt's.
- **Copies of a project share a cache folder.** A project keeps its id
  when saved under another name, and the cache folder is by id. Opening a
  project clears the branch states none of its branches have, as Chimera
  does, so opening one copy can clear a state only the other copy's branch
  had. That branch is then replayed to, once; nothing is lost.

## The rest of the move, by size

The C# interface by area, in lines (Designer files are generated layout,
which QML replaces):

| Area | Lines | Becomes |
|---|---|---|
| config (settings, wizard, firmware, Core Manager, paths) | 14,979 (3,783 Designer) | view models over the engine's project, package and firmware calls; QML forms |
| TAStudio (piano roll, branches, markers, bookmarks, undo) | 11,185 + 363 | pianoRoll, session and branches (markers, branches and undo done) |
| MainForm (menus, files, hotkeys, movies, MCP hooks) | 10,167 | the main window and session (started), hotkeys, recent files |
| movie (formats, import, Bk2) | 7,548 + 1,045 | mostly the engine's already (movie log, import); the rest moves there |
| Lua (console, libraries, NLua) | 7,005 + 4,197 | the console (started), the libraries as view model commands, Lua 5.4 directly |
| Watch, RAM Search | 6,800 + 2,620 + 325 | RAM Search (done), RAM Watch, cheats |
| custom controls, theming | 6,766 + 3,631 | Qt's controls; the shipped themes as palettes |
| AV out (recording video) | 6,624 | a view model over an encoder; separate work |
| hex editor, debugger, trace logger | 3,217 + 2,203 | C++ items like the piano roll |
| display, input, sound | about 9,000 | done here (picture, keys, SDL pads, OpenAL) |

About 150,000 lines of C# become about 40,000 to 60,000 of C++ and QML, if
the prototype's ratio holds. The prototype is 7,900 lines of view models,
2,200 of Qt (1,000 of them QML) and 1,700 of tests.

A suggested order, each step usable on its own:

1. **TAStudio's remaining parts.** Markers, branches (with a state file
   each, so loading one is a state load), undo and the greenzone kept with
   the project are done. Still to do: saving everything else a project
   holds (subtitles, headers, the piano roll's layout), and the rules for
   a machine a GPU drew once the window has the GPU bridge. This is what a
   TAS author lives in.
2. **New project wizard, firmware and Core Manager.** Opening anything
   without the C# window first. Done: the Cores window, firmware found by
   the engine's rules, a project made of an open game, a game opened by its
   kind of file, the New Project window. Still to do in it: settings
   suggested by a core, presets, a game core's own firmware, movie import,
   and the project written only when the work is first saved (here it is
   written when made).
3. **RAM Watch, cheats, hex editor.** RAM Watch is done, but for game
   property watches (kept in its files as they are, not read yet) and
   poking a value.
4. **Lua's libraries** (memory, joypad, gui drawing, forms) as view model
   commands, so MCP has them too. Coordinate with the New-Features
   session, whose the MCP code is.
5. **Movie import, video recording, the debugger and trace logger.**
6. **Hotkeys and their configuration,** themes, recent files. Then the
   Qt window is the default, and the C# one goes.

## Yours to decide

1. **How Qt is obtained for Windows.** Here it is built from source with
   Ubuntu's mingw (posix threading), in 12 minutes, without the network
   module. The other way is Qt's own prebuilt Windows libraries, built
   with a different compiler.
2. **When this leaves the private branch:** a public branch, CI, a
   preview build beside the C# window.
3. **macOS:** there is no Mac here; CI runners are the only way to check
   it.
