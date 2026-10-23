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
  past it, goes back before it and plays the movie up to where it was. As
  in Chimera's TAStudio, "where it was" is the frame the machine was on at
  the first of a run of edits, kept until the machine is moved some other
  way; going back adds no frames to the movie (one made shorter is followed
  to its end, and an undo that gives the frames back lets the machine go
  on). The window edits while the machine thread seeks, so a seek may find
  frames taken out under it: it then stops quietly, since the edit that
  took them sends the machine where it belongs next. The
  window plays and seeks with the engine's own calls
  (`ce_session_movie_advance`, `ce_session_seek`). The other ways - an
  export of the engine's line writer to the window, or a copy of the
  format in the window as the C# one has - were set aside. One more call
  follows from it: `ce_session_movie_at` says that a state the window
  loaded itself (a branch's) put the machine on a frame of the movie, so
  the engine counts from there instead of replaying to it. And the rule
  for which machine a project makes (core build, settings, firmware,
  files), which decides whether kept states may be loaded, moved from the
  C# into the engine as `ce_project_machine_id`; frames are put in and
  taken out of the engine's movie by `ce_session_movie_insert` and
  `_remove`; and so did rerecord
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

The main window is Chimera's: its menus are Chimera's, in its order
(File, System, View, Config, Tools, Emulator, Help), with System,
Emulator and the status bar there only while a machine runs, and the
debugger, surface viewer and trace logger in the Emulator menu, as tools
the core offers. As in Chimera, the way in is a project: File has New
Chimera Project and Open Project, and no way to open a game alone, and
there is no window to change a machine's settings once it runs (they are
chosen when the project is made). With nothing open the window shows
Chimera's idle eye: its mark, very faint and colourless, blinking now and
then, its eye following the pointer across the window (a view model makes
the picture, only when it would change). A game file dropped on the window
starts a new project from it. (The command line still opens a package and
a game, for the tests.) Capture OSD puts the counters and messages over
the picture into screenshots, as Chimera's does. Config > Speed/Skip has
Chimera's three throttles, one at a time or none: the clock (the
machine's rate, at the speed asked for), the sound as it is heard (the
machine waits while the sound queued is ahead), and vsync (a frame for
each picture the window shows); VSync Enabled sets the window to the
screen's refresh from the next start (Qt sets that when the window is
made).

Where Chimera has a window of its own the prototype has one too, under
Chimera's title: Controller Config and Configure Hotkeys are two windows
(they were one, with tabs), and TAStudio's Metadata menu opens Header
Info, Edit Comments and Edit Subtitles each in its own window (they were
one Project Details window).

The main window shows the picture alone. TAStudio is a window of its own,
as in Chimera: it opens when a project opens, at the main window's right
side with their tops level (moved left as far as needed to stay on the
screen), and closes with the project. Its menus are Chimera's TAStudio
menus (Edit, Metadata, Settings, Columns, Window) with "<< Hide" at their
right; hidden, the main window shows "Show TAStudio >>" at the right of
its menus. Beside the roll are Chimera's boxes: Playback (previous
marker, a frame back, pause or run, a frame on, next marker, follow
cursor, auto-restore), Greenzone (every frame, every N frames, off),
Branches and Markers, each with a few buttons and a right-click menu for
the rest. Every movie has Chimera's three markers of its own: Run start
on frame 0, Last input on the last frame anything is pressed on, Run end
on the movie's last frame. They follow every edit and every frame
recorded (worked out as Chimera does: after an edit, walking back from
the end no further than the edit unless it could have erased the
answer), are never saved, cannot be moved, renamed or removed, count
for next and previous marker, keep their states as a marker does, and
are coloured apart from the person's in the roll and the list. TAStudio
Settings' Rotate input roll turns the roll on its side as Chimera's
does: the frames run across with their numbers turned, the buttons down
the left by name, the scroll bar along the bottom; it is kept in the
project, as Chimera keeps it. A right
click on the frames picked opens Chimera's menu for
them: set markers (more than 50 asks first), a marker with text, remove
markers, deselect, select between markers, clear greenzone, copy, paste,
paste insert, cut, clear, delete, insert, clone, truncate, and branch. Keys pressed in TAStudio reach the machine and the hotkeys, as
Chimera's TAStudio passes them on. Move with Main Window (on at first, as
in Chimera) brings TAStudio along when the main window is dragged (not with Shift
held, as Chimera's).

| View model | What it does | Window |
|---|---|---|
| session | open a game (the installed core that takes its kind of file opens it; when several do, they are offered), open, save and make projects, recent projects, play, pause, frame advance, restart, seek, keys and gamepads, sound, the greenzone (kept with the project in its cache folder and loaded when the project opens, unless the last session did not end; TAStudio's Greenzone box - every frame, one in N, none - saved in the project's TAStudio part as Chimera saves it), settings applied live or by restarting; the project's changes not saved yet (a star in the title, and Chimera's "Save Chimera Project?" before it is closed, another opened or the program quit); TAStudio's playing and saving settings (pause at the movie's end, the rewind steps, the autosave and its backups, a backup with every save by hand); the View menu's displays: the average speed since it was turned on (the engine's count, on the speed's line), the rerecord count, messages over the picture a moment each, the status bar shown or not, the window sized 1x to 10x the picture, a project's files not found beside it nor where this machine last had them (local-paths.json in its cache, checked by hash) asked for as Chimera's resolution window asks: located one by one or found by scanning a folder (by hash, then by name), a file that is not the recorded one opened only when asked (Open Anyway), or the project left unopened, and a log window of every message the window has shown (Chimera's shows its console instead); Chimera's Emulator menu (Game for a game core): a game's own timer over the picture, the save data written out (one file as it is, more in a zip), a movie import opening New Project on the core; Chimera's File > Screenshot (the Screenshot key's file, Screenshot As to a PNG or a JPEG named where asked, the picture or the window to the clipboard); Chimera's Save Config (the settings are written as they change, so it says where), Save Config As (a copy), Load Config (the file read again) and Load Config From (a file's settings set, and kept as the window's own); Chimera's greenzone budgets (from the Cache Manager): what the history of states may keep in memory (4 GB at first; it was a fixed 1 GB here before), how close it stays behind the playhead (one frame in 4) and how many frames after a load show kept pictures (3), every project's and remembered, and a project's own memory and closeness in budgets.json beside its cache (Chimera's file and names), each taking effect when a machine opens; Chimera's Sound window: the sound device (the ones OpenAL lists, or the system's own), the buffer in milliseconds (100 at first; a frame's sound further ahead than that is dropped), the sound at the machine's own speed and while fast forwarding each on or off, the fast-forward volume as a share (50%), frame advance muted, each remembered (only the OpenAL output method is here, as it is Chimera's only one); Chimera's Speed/Skip: fast forward at its alternate speed (400%, remembered; it was unthrottled here before), a frame skip of 0 to 9 undrawn per frame drawn while running, only to catch up when auto-minimized (on by default), unthrottled drawing one frame in four, never skipping while frame advance runs on; unsaved work kept against a crash as Chimera's ProjectRecovery keeps it (a snapshot of the project every five seconds while it has changes, in a Recovery folder beside the caches, gone when it is saved or closed), and a crash's work rebuilt as a copy among the backups and offered when the project next opens ("Recover unsaved work?": yes opens it unsaved under the project's name), a folder whose writer still runs left alone as another window's | main window, TAStudio Settings, Log Window |
| pianoRoll | a row a frame and a column a button; press, drag, paint, fill; frames picked (on the frame numbers, as in TAStudio, with the thin column before them moving the machine), put in, taken out, cloned, cleared, copied, cut and pasted (over or in) through the system's clipboard as the movie's lines (with their frame numbers when asked; a line that is not a frame's input pastes nothing), cut off, with TAStudio's keys; analog controls a column each with their values, a cell picked to edit and a drag painting its value; markers that can move with the frames, and keep their frame's state or not by default; undo and redo (a stroke is one step), and the undo history by Chimera's step names (undone or redone to a step, forgotten up to one, the most kept chosen); follows the current frame the four ways TAStudio scrolls to it (into view, to the top, the bottom or the middle; every frame or only when it is out of view) and scrolls the wheel's frames a notch; edits that leave the hidden columns alone when asked (Chimera's "allow editing hidden columns" off: clear, paste, put in, take out, cut off and recording change only the columns shown); a column painted with its pattern (Alt held, or always, or on the Auto-hold columns only, as TAStudio's Paint Patterns say), each control's own pattern kept in the project as Chimera keeps it (runs of values, the one it goes back to, lag frames passed over), the Columns menu as Chimera's (the console's columns, a menu a player's, the keyboard keys thirty a menu, a toggle a group; a column shown or hidden marks the project changed), a column's menu on its name (Auto-hold, hide, show all) with the Auto-hold columns marked: while the game plays an Auto-hold column's button is held, fired or follows its own pattern as "Choose pattern" says, and an analog one holds the value asked for when it was turned on (chimera issue 298), all kept in the project as Chimera keeps them; rows coloured as Chimera colours them (the current frame, the greenzone and its lag frames apart where a state is held, the frames an edit took out of it, every second player's columns striped), states and markers told by colour or by an icon, the roll's own colours over the theme's and its own font (TAStudio's Set Colors and Set Font); lag frames hidden as Chimera hides them (of a run, the first N; frames that were lag before an edit too, when asked; kept in the project); macros saved from the frames picked and placed at them, as .chimeraMacro files Chimera reads and writes (only the macro's columns written; pressed over, or put in before, when the file says so), and the recent ones | TAStudio (a C++ item draws the roll) |
| branches | other versions of the movie, kept in the project, each with its state in a file in the project's cache folder and the picture it had; loading one loads that state, or replays from where the two movies differ when the state is missing or is another machine's; loaded by a double click, or only gone to with the old control scheme (as Chimera does when not recording); a branch's picture shown while the pointer is over it, no side longer than the branch screenshot size | TAStudio's Branches box, TAStudio Settings |
| ramSearch | the engine's RAM Search, updated after every frame, laid out as Chimera's: its menus (File, Options, Search, Settings, Columns), toolbar and right-click menu; Compare To / By with a box each (previous value, specific value, address, changes, difference) and the Comparison Operator in Chimera's order; Detailed or Fast (a domain of 1 MiB or more starts Fast, and Fast hides the change counts), Check Mis-aligned, the domain, size, byte order and display (a new size reads the search again), Define Previous Value; the preview colours the rows the next search would remove; Auto-Search after every frame, not after a lag frame when told so; undo and redo, Copy Value to Prev, Clear Change Counts, Remove Selected, Go to Address, Add to RAM Watch, RAM Poke (the same window as RAM Watch's), View in Hex Editor, sorting by any column; a search kept in, read from, added from and truncated by .wch files; the warning for addresses past the end and the button for a setting that needs a new search; Chimera's messages and its question before acting on more than 1,000 rows. Freeze Address waits on question 5 | RAM Search |
| ramWatch | addresses kept an eye on, read after every frame, shown unsigned, signed, in hex, in binary, as fixed point or as a float exactly as Chimera writes them; the previous value three ways, changes counted; Chimera's .wch files both ways (other emulators' too); a game core's properties watched by name, read and written as text by the engine; the marked ones shown over the picture where Chimera shows them. The window is laid out as Chimera's: its menus (Files, Watches, Settings, Columns), toolbar and right-click menu; rows picked one, several or a range at a time; New, Edit and Duplicate Watch in Chimera's editor (several watches edited at once, the byte order left as it is when they differ); Split Watch (a word or double word in hex or binary into halves), RAM Poke, Edit Separator, Add Game Properties, Purge Duplicate Watches; copy and paste as .wch lines; moves up, down, to the top and bottom; Default Domain; a click on a heading sorts the stretch between separators as Chimera does; Define Previous Value, On Double-Clicking a Watch, which columns show, recent files, all remembered; the warning for addresses past the end of their domain, clicked to take them off; Chimera's status line. Freeze Address waits on question 5, and Set Read and Write Breakpoint are not here, as no core has memory callbacks | RAM Watch |
| settings | the package's declared settings, those that apply, edits applied live or by restarting; the package's presets for the machine, written in as if typed (never the machine's own setting; a name the machine lacks named) | none: Chimera has no window for a machine's settings once it runs (they are chosen in the New Project window); scripts reach this one |
| luaConsole | Chimera's Lua Console, laid out as Chimera's: a list of scripts, each started and stopped (toggled), paused, moved, duplicated, edited in the person's editor or taken out, with separators between them; the count of scripts above the list; sessions (.luases) written and read as Chimera writes them, the paths beside the session's file relative to it; New Script from the Lua folder's .template.lua; recent sessions and scripts; Chimera's settings (Disable Scripts on Load, Toggle All if None Selected, Reload When Script File Changes, Duplicate to Stdout); the output, at most 100 writes a frame as Chimera's, its lines picked and copied; the line typed into, an expression's values printed and Up and Down through the lines typed before; Active Registered Functions (call, remove, remove all) and the Lua Functions List (each library's functions with their parameters). Closing it stops every script, as Chimera's | Lua Console |
| toolWindows | what Chimera puts at the end of every tool window's Settings menu (in a Window menu where the tool has one, as TAStudio does): Save Window Position (on at first: the window opens where it was left, at its size; TAStudio keeps only its size, as it opens beside the main window), Stay on Top (Windows only, as Chimera's), Float From Main Window, Restore Defaults (the tool's own settings too); File > Close where the tool has a File menu. The Lua Console and TAStudio have them; the other tool windows get them as each takes Chimera's menus | every tool window |
| newProject | a project made before any machine runs, as Chimera's New Project window makes one: the core among the installed packages - emulators or game cores (remembered), a line a package with its version dated in Chimera's clock (issue 331), the package's hash where two are of one core, and its systems -, its machine, its renderer (asked beside the machine and named for what it is, with what a hardware one costs, as Chimera asks it), files in the slots its package declares (kinds - narrowed to those the chosen machine claims, as Chimera does -, how many, the ones a choice rules out, at least one of a group, names a slot asks for, a cue's tracks checked at once), the settings that apply (with the options and default the chosen machine narrows a setting to, its `settingOverrides`, as Chimera has them), the firmware found by the engine's rules or chosen by hand (a file whose hash is not the declared one is refused, except by a game core, whose project then pins that file's own hash; a file chosen by hand is remembered, as Chimera remembers it, so the project finds it when it opens; Scan Folder looks through a folder, with its subfolders or not, for what is still missing - by hash where the package gives one, else by the declared name - and that folder becomes the firmware folder when the one set is not there, as in Chimera (issue 293)); a movie made elsewhere imported (the core's movieImport: its answer, asked in a process of its own through chimera-run, fills the form, and the project takes the movie's inputs); the settings a core suggests for the game (asked the same way, once a game, with the files, slot map and settings chosen so far, taken back for another game, never over an import's), and what the game has of its own: settings after the package's and, for a package that says `gameFirmware`, firmware after the package's (one by a package setting's name or a package firmware's id is dropped); a project's open asks the core again what its game needs, as Chimera does; what is missing said a line at a time; made and opened, written nowhere until its first save, which asks where it goes (as Chimera does); opened on the last project's answers - its core, machine, settings and the files still where they were found, the firmware where this core last had it - with the core still asked about the game but its values not put over the project's own (an import is treated the same); another game then takes back the settings that still hold what the core suggested for the project's game (Chimera issue 214), and a game's own setting values go when another game comes; a file dropped on the main window starts it, as in Chimera: the first core that takes its kind (on the last project's answers when they are that core's), the file in the slot that takes it - in place of what a full slot held, where Chimera adds it beside and complains | New Project |
| hexEditor | Chimera's Hex Editor, laid out as Chimera's: its menus (File, Edit, Options, Settings) and right-click menu; a box titled with the system, the domain, its addresses and what is being typed; the column numbers, the addresses, the values 1, 2 or 4 bytes at a time in either byte order, and the text they spell (as ASCII, or through a .tbl file); only the lines on screen read, after every frame. One address highlighted and others beside it (Ctrl, Shift, a drag), the keys moving as Chimera's (arrows, Page Up and Down, Home, End, Tab, Escape, with Shift taking in what they pass); hex digits typed written in once a value's worth is in; Increment and Decrement; Copy (the bytes), Export (the values), Paste; Find (hex or text) with Find Next and Find Prev, as Chimera looks; Go to Address, Add to RAM Watch, RAM Poke; Save as binary, Save as text, Import as binary; File on Disk (the game opened by itself, edited in memory and saved back); Custom Colors, remembered with the domain, size, byte order and recent tables. Freeze Address waits on question 5 | Hex Editor |
| controls | Chimera's Controller Config: the buttons' keys and gamepad inputs (Normal Controls), the keys that fire a button while held (Autofire Controls: pressed on the frame the key goes down and then as the autofire pattern says, counted from that frame), and what moves each analog control (Analog Controls: a gamepad axis caught by wiggling it - 20000 of movement, as Chimera's - or the pointer over the picture, Bind!, Unbind!, Flip Axis, sensitivity -200% to 200%, dead zone 0% to 50%, and a key for either end, worked out as Chimera's LatchFromPhysical); the package's at first, from its default_keybinds.json groups (AllTrollers, AllTrollersAutoFire, AllTrollersAnalog). Laid out as Chimera's: the three kinds in tabs (Analog only when the controller has analog controls; Feedbacks left out: the engine has no rumble), a tab a player inside each when there is more than one ("Player N" for "PN ...", else "Console"), a box and its name down columns, the box taking the next key or gamepad input pressed while it has the focus (Escape empties it; Auto Tab: the key replaces the box's keys and the next box takes the next, off it is one more), its arrow giving Escape and the pointer's buttons (which hold what they are bound to while pressed over the picture), the package's picture of the controller beside them (its "picture", the machine's own when the package is more than one), the tips, Auto Tab, Misc... (Load Defaults, Clear), Save and Cancel; the window's title is the controller's name and "Configuration", and it works on a copy, kept by Save. What differs from the package is remembered per core. Not here: the pointer as a relative axis (RMouse X/Y, Chimera's captured mouse), and a game's own names for its controls beside theirs | Controller Config |
| help | Chimera's Help menu: the site in the browser, the build and the core open in a bug report's words (Copy Version Info), a report's first lines filled in from the session (build, core and version, system, processor and memory, the project's files, the settings off their defaults) with the files worth attaching and a zip of them all and the game's picture (Report an Issue; nothing is sent, and the core log is listed while it is kept), and About; Tools > Export Core Log (the session's: the engine keeps what the cores write in a file only when asked, never remembered, a restart offered so it covers the core from its start) | Help menu, Report an Issue, Tools menu |
| batchRun | Chimera's Batch Runner: games dropped or added run in turn for a number of frames, each with the installed core that takes its kind of file, in the headless runner (one machine to a process), what came of each listed (Success, ErrorOnLoad, ExceptOnAdv; frames, lag frames, SHA-1) and dumped as Chimera's dump writes it | Batch Runner |
| mediaMaker | Chimera's Reproducible Media Maker: a folder packed into one file the same on anybody's machine (a stored zip, an ISO 9660 + Joliet disc, a FAT12 floppy), named beside the folder, how far it has got, stopped when asked, its SHA-1 said; an installed core's disc recipe used for a folder it is for (the engine packs: ce_media_make) | Reproducible Media Maker |
| customize | Chimera's Customize window, for what the prototype has: start paused (the first machine waits; after that a machine opened runs when the last was running, as Chimera's pause is the person's), start full screen, pause while a menu is open (a run-state action picked from it stands), run in background (off, the machine pauses behind other windows and runs again in front), background input and skipping lag frames (the session's), never ask to save, scripts during turbo, the key priority cycle without "both"; each kept at once and remembered. Not here: the context menu, alternate keyboard layouts, merged L+R modifier keys, single instance, the sandbox integrity checks and the GameCube adapter | Customize |
| messages | Chimera's Messages window: where each of the nine things over the picture goes (the speed, frame counter, lag counter, input, watches, messages, rerecords, autohold, game time), as a corner and a distance from it, Chimera's places at first; the four colours (a colour left at Chimera's default is the theme's, as Chimera's OSDManager decides); how long a message stays; messages stacked (the newest at their corner) or only the newest; Default; each kept at once and remembered. The previous frame's input colour is kept but nothing here draws that input yet | Configure On Screen Messages |
| display | Chimera's Display window, for what the window draws: the picture's shape (its raw pixels, the machine's own, a size or a ratio), kept with bars or stretched, grown by whole steps only or freely, its edges cropped, drawn sharp or smoothed (bilinear); Display Nothing and the status bar; the window's place and size kept for the next start when asked; all remembered. The fitting is worked out in the view model and tested; the window draws the rectangle it gets. Not here: the bicubic filter and shaders (hq2x, scanlines, the person's own), prescaling, the menu, caption and frame chosen per windowed and full screen, hiding the mouse, double-click full screen, fixed window titles, the snow over no game | Display |
| paths | Chimera's Paths window, its global folders the prototype uses (ROM, Firmware, Movies, Movie backups, A/V Dumps (where Encode Video suggests a video goes), Screenshots, Lua, Themes, Watch, Bug reports, Macros): each absolute or relative to the base folder (the program's own at first, as Chimera's "."), changed ones remembered, "Use recent for ROMs"; the backups, the firmware folder (both ways with the Cores window), screenshots and themes follow them, and the file dialogs open in their folder, or the nearest one above it that is there (nothing is made). Not here: Chimera's per-system folders (savestates, palettes, user), debug logs and temp files. Tools and Debug Logs are there as in Chimera, and a relative Watch or Debug Logs folder is under Tools, as Chimera resolves them | Paths |
| firmware | Chimera's Firmware window: every installed core's declared firmware, asked of the packages when the window opens, each with what is on hand - right (its hash, or its name when the core declares no hash), not a dump the core knows (for a game core, the person's own), chosen but gone, or nothing; a file set or cleared (remembered where it is, never copied); Scan Folder (and below it) filling what has nothing usable, never replacing a file the person chose. Not here: Chimera's grouping of an id declared many times, and the kind filter | Firmware |
| dataFolder | Chimera's Data Directory: one folder for what the window keeps for the person (Projects, each project's cache; Recovery, unsaved work), the system's place at first; a change is recorded and carried out at the next start before anything in it is open, the data moved with it when asked (a rename where it can be, else a copy checked before the old goes; a failure leaves the old whole); a folder holding other things gets a Chimera folder inside it; into or out of itself refused. The caches were in the system's cache folder before; they are regenerable, so the old ones are simply left | Data Directory |
| precompiled | Chimera's compile cache and its Pre-Compiled Modules window: for a core whose package says "precompile" (RPCS3), the engine is given a folder for the game, named by its SHA1 under Cache/PrecompiledCode in the data folder (before, the prototype gave none, so such a core compiled at every boot); the window lists those folders, named, dated and counted by their manifest.json when there is one, and removes the ticked ones, never the open machine's. Not here: the wizard's compile step that writes the manifests (so folders filled at a boot show as not named), and the compile sessions' memory setting | Pre-Compiled Modules |
| traceLogger, debugger | Chimera's Trace Logger: the core's own line for each instruction its CPU runs (what ran, then the machine around it after a tab), drained each frame through the engine, the latest 10,000 kept (100 to a million), what is to be kept written to a file as it comes, the visible lines saved; the core traces only while logging, and stops when the window closes. The Debugger's register box: the core's registers, read as the machine moves (ten times a second at most) and set by hand (hex), with the cycles run when the core counts them. The Surface Viewer: the pictures a core draws for itself, picked by name, 1x to 8x, drawn again as the machine moves while the window is open. The synthetic core now traces, shows R0-R7 and its frame counter, and draws its RAM as a 64x64 picture, so all three are tested. Not here: Chimera's disassembler, breakpoints and memory callbacks. The Trace Logger's window is laid out as Chimera's: the File menu (Save Visible Lines...), the toolbar with Start Logging and Log to File... as buttons that stay down, Clear and Keep, the Disassembly and Registers columns, and Chimera's status line (the core's header, the lines buffered, those written to the file); its dialogs open in Debug Logs | Trace Logger, Debugger, Surface Viewer |
| macroTool | Chimera's Macro Tool (Macro Input): the entire movie and zones of it (a start and an end frame, or a .chimeraMacro loaded), each named, with Overlay and Replace and the columns it carries; placed at a frame (one undo step), saved as a .chimeraMacro Chimera reads - a zone of some columns written as Chimera's LogEntryGenerator writes one (grouped by player, axes before buttons) - and zones not saved asked about on closing. The piano roll's Save Selection to Macro and Place Macro share its code. The window is laid out as Chimera's: the File menu (Save Selected As..., Load Macro..., Recent, shared with TAStudio's Recent Macros), the start and end with Set Macro, the zones with the entire movie first, the name, frame, Place Zone, Replace and Overlay, and the controls ticked along the bottom placed as Chimera places them; asked on closing, Yes saves each macro not saved | Macro Input |
| encoder | Chimera's File > Encode Video, as Chimera's own design has it (its docs/encode-video.md): a video is an encode of a run, made in one act. The window asks for everything at once: the file, the ffmpeg command (typed, or one of Chimera's nine presets; the box names the preset the command is, and the file takes the extension of the container the command names), the two markers to encode between, both included (it opens on Run start to Run end), a size of its own (padded or stretched, nearest pixel) and whether to sync to the sound (on at first, as Chimera's). Start Encode keeps where the machine is as a state file the engine writes in the project's cache folder, winds the run to the first marker, plays it read only as fast as it goes, keeping no state, pipes every frame and its sound to ffmpeg as a NUT stream (Chimera's muxer, ported), and gives the machine back on its frame, paused or running as it was. A video already at that name is not replaced without asking; progress, frames a second, time left and the sound's levels are shown; Stop and closing the window end it; a picture whose size changes starts a new file. The ffmpeg is the one the bundle ships beside the program (the build's in build/dll). Scripts have client.encodevideo and client.encodingvideo, as Chimera's. A project's run of frames 5 to 14 was encoded and counted by that ffmpeg: 10 frames, both ends included, the machine back on its frame. Chimera's File > AVI/WAV, which the prototype had copied from the BizHawk lineage, is gone, as it is from Chimera. Not here: Capture OSD and Capture Lua (what the window draws over the picture is the window's, not the machine's thread's) | Encode Video |
| Lua libraries | Chimera's Lua libraries as a layer of Lua over the view models (lua_libraries.cpp), with Chimera's names, arguments and table numbering (from 0 where Chimera's are): console, bit, emu (frameadvance and yield, framecount, lagcount, islagged, the system and core, registers read and set, cycles, the frame rate limit), memory and mainmemory (domains, bytes, ranges as tables, every size, sign and byte order, floats, hash_region's SHA-256 as Chimera gives it; the default domain the system bus when there is one), joypad (get, set for the next frame, setaxis for an analog value on the next frame, setanalog for one held until let go, setfrommnemonicstr for a movie line's buttons and values), movie (loaded, file, length, mode, rerecords, input of a frame by full or controller names, save, play from start, stop), client (pause and play, speed, frame skip, reboot, open and close, sound, messages, autohold), gameinfo, event (onframestart and onframeend, both after each frame as the window learns of it; onexit; onconsoleclose; unregisterbyid and unregisterbyname on the script's own; the events part way through a frame refused with Chimera's words, so that a script can guard them with pcall), memorysavestate (the core's own state kept in memory by an id, and loaded back on its frame), userdata (values kept for the session, shared by every script, as Chimera's, in memory; Chimera's --userdata k1:v1;k2:v2 fills it at the start, read as Chimera reads them), SQL (a SQLite database made, opened, written and read, every value keyed "column row" and every answer in Chimera's words; the SQLite Chimera already builds for its C# side), comm (data shared with other programs on this machine through named memory-mapped files - a Windows file mapping's name, on Linux /dev/shm/<name> - written, read, copied to and from memory, and the picture as a PNG to the file --mmf or mmfSetFilename names; no network, as Chimera's), forms (a script's own windows: forms with buttons, labels, text boxes, check boxes, dropdowns sorted as Chimera's, and picture boxes drawn on as gui draws, shown on refresh; clicks and a form closed by the person call the script's functions; text, place, size and WinForms' common properties read and set; openfile is not here), tastudio (engaged when a project is open; the edit queue - buttons, analog values, frames put in, taken out and cleared - applied together, each edit its own undo step where Chimera makes them one; the lag log read and set; whether a frame has a state; clearing the greenzone; going to a frame or a marker by its text; the frames picked; markers set, read, found and removed, the run's own among them; branches listed with an id, frame and text, renamed, the next one's text given, loaded, and a frame of one's input read; recording mode is question 6's, so it reads false and is not set; functions called when the greenzone is invalidated (every edit and frame recorded, with its first frame) and when a branch is loaded, made or updated, or removed (with its place); a script's own columns (addcolumn) and the colour and text it gives any cell (onqueryitembg, onqueryitemtext; the first answer winning), asked while the roll is drawn - the window's thread waits then, so the script is not running - and gone when the script stops; and an icon from an image file for any cell (onqueryitemicon), each file read once until clearIconCache), game (a game core's properties by name, in the core's order: read and set by the engine's rules, an array by its own name as a table, bytes as a table of byte values, and described; a name the core lacks said in the console, as Chimera's), input (the keys held whatever they are bound to, the gamepads' buttons, the pointer over the picture in the machine's pixels with its buttons and wheel; the gamepads' sticks are not followed), gui (rectangles, boxes, ellipses, lines, axes, pixels, polygons, Bezier curves, pie slices, text in the machine's pixels and in the window's from a corner, images and parts of them read from files - a relative path from the script's folder, as Chimera runs a script in its folder -, the default colours, messages; drawn over the picture by the window, scaled with it, and gone when the next frame begins, as Chimera's); each script runs as a coroutine of its own, so where it waits for a frame it goes on after the next; what a script registers is its own (the event functions, tastudio's, its forms), called as that script and gone when it stops, and a script whose code has ended runs on while it has any, as Chimera's; a script's code runs in its own folder, so its dofile, require and io.open find files beside it; console.log and print put tabs between values and write a table a key a line, as Chimera's; Chimera's Toggle Last Lua Script. Not here: gui's other surfaces (all is drawn over the picture) and an image read again at every call (cache false), forms.openfile, and the timing question 4 asks | Lua Console |
| cacheManager | Chimera's Cache Manager for the project caches and the unsaved work kept against a crash: each named as its project says (about.json, which the session writes beside the cache when a project is saved or opened), its size, the projects no longer where they were last seen picked out, what is in use kept, the ticked ones removed after a question naming them; Chimera's limit (100 GB, and 20 GB of the disk left free, never below 1 GB for the disk's sake) held by the auto-clean at the start and when a project closes, the oldest unlocked first, never the newest or the project just closed; Lock / Unlock (project caches start unlocked, unsaved work locked; cache-locks.json in the cache folder), Clean Now, and the line saying why the cache is over its limit | Cache Manager |
| projectDetails | the open project's header (author, emulator version, board name, game name; the core shown, not edited, since its package pin decides it), its comments, and its subtitles - a line from a frame, for a number of frames, at a place, in a colour - kept with the project, the frame's subtitles worked out for the window to draw; a new project starts with the default author, as in Chimera | Header Info, Edit Comments, Edit Subtitles (three windows, as Chimera's TAStudio Metadata menu opens them) |
| hotkeys | the window's actions on keys, with Chimera's names, keys and order: frame advance (held half a second it runs on), rewind (held it goes on back), pause, fast forward, toggle throttle, screenshot (a PNG named and numbered as Chimera names one), full screen, open, close, load last, reboot, volume up and down, faster, slower and its own speed (Chimera's steps, 1% to 6400%), sound on or off, exit, the tool windows, RAM Search's six, branches (add, slots F1-F10 and Shift+F1-F10), seek to a frame, follow, auto-restore, greenzone, undo and redo, markers (to the previous or next, set, delete, select between), select all, and the frame edits (clear, delete, insert, insert a number, clone, clone a number of times, paste insert), show the current frame, select it, seek to the frame picked, back to where the edits began (Chimera's green arrow), pick the copied frames again, the screen or the window to the clipboard, hide the menu, stay on top, TAStudio shown again; changed in Configure Hotkeys and remembered. That window is laid out as Chimera's: Find (a name in any case, the names that start with what is typed suggested), a tab a group in Chimera's order with the actions in Chimera's order, a name and its box down columns (the box as Controller Config's, taking combinations such as Ctrl+S: a key is bound when it is let go, so Ctrl alone can be), Chimera's note at the end of the TAStudio tab, the tips, Auto Tab, Misc... (Restore Defaults, Restore Current Tab Defaults, Clear All, Clear Current Tab), Save and Cancel; it works on a copy, kept by Save; a key that is a hotkey and a button does both, as in Chimera. A menu's action is run by its hotkey, and the menu shows the keys bound to it (a relay hands the window what is the window's: a menu, a dialog, a window). the counters over the picture (speed, frame counter, lag counter, input display: where and as Chimera draws them, each on or off and remembered). Turbo (held: as fast as it goes, nothing drawn or heard, the speed marked >>>>), the window a size larger or smaller (1x to 10x), Frame Inch (paused a frame, running a pause), messages on or off, Skip Lag Frames (a frame advance that lands after a lag frame runs on to one that is not, 600 at most; remembered), Display Nothing (frames run undrawn, the picture and counters black, the speed in the title; remembered), background input (a gamepad heard while the window is behind others; remembered; the keyboard is only ever the window's in front). Chimera's Key Priority for a key that is a hotkey and a button (both act, the button wins or the hotkey wins, each giving way only for a key the other side has, as Chimera's InputManager decides), from the Emulation menu, the status bar (a click is the next one) or its hotkey; remembered. Not here yet: those whose feature the prototype lacks (read-only and recording - an owner's question below -, capture mouse, the last Lua script, frozen addresses). Chimera's Movie group as TAStudio answers it: Play Movie (open a project), Record Movie (New Project), Stop Movie (the project let go and its game opened alone, a new empty movie), Play from beginning (the project read again from its file), Save Movie (the project saved). Not needed: Cancel Seek and Toggle Turbo Seek - a seek here is the engine's own replay, drawn only where it lands, so it is always what a turbo seek is in Chimera | Configure Hotkeys |
| themes | Chimera's colour themes: its built-in Light (the desktop's own colours) and Dark, and every theme file in the themes folder; one chosen (Dark at first, as Chimera's default) and remembered; the window's palette and the piano roll's TAStudio colours follow it | Themes |
| cores | the installed core packages and the files each takes, the cores and firmware folders; a project's core is found here, and its firmware by the engine's rules | Cores |

The tests drive the view models with no window: 1,029 checks on the
synthetic core (and on four packages made from it at build time: one under
another name, one of two machines with declared slots and a firmware, and
two whose firmware declares the hash of its file, one of them a game core). They include a project whose movie ends with the machine's
memory byte for byte as the witness recorded it. One more test loads the
whole window off-screen and runs it for a second. Every new check was
broken on purpose once and seen to fail.

## What was measured

- A project made by this window with the greenzone every 5 frames opened
  in Chimera's own TAStudio (Mono, Xvfb) with no warning, which read the
  choice and saved it back with its columns; opened again here and saved
  with another choice, every field Chimera had written was kept.
- A real DOSBox-X project made with the New Project window from a script
  (a configuration file whose autoexec prints a line and the DOS version)
  boots and runs it, at the machine's own 70 frames a second (Linux,
  off-screen).

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
- **The piano roll was put in the main window.** From the first
  prototype the roll sat beside the picture in the main window, with
  TAStudio's menus in the main menu bar. In Chimera, TAStudio is a window
  of its own; the owner pointed this out on 2026-10-23. The roll, its
  menus, the side boxes and TAStudio's own windows (Header Info, Edit
  Comments, Edit Subtitles, Undo History, TAStudio Settings) are now in a TAStudio window, and the
  separate Branches window is gone: the branches are TAStudio's Branches
  box, as in Chimera.
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
| Lua (console, libraries, NLua) | 7,005 + 4,197 | the console (done), the libraries as view model commands, Lua 5.4 directly |
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
   kind of file, the New Project window, movie import, suggested settings.
   Presets too, a game core's firmware of the person's own, a game's own
   settings and firmware from a suggestion, a project written only when
   it is first saved, and opening on the last project's answers, with
   Chimera's take-back for another game (issue 214). Still to do in it:
   the compile step (a core compiling a game's code before the project
   is made), and a check of Chimera's window, part by part, for what
   else it has.
3. **RAM Watch, cheats, hex editor.** RAM Watch and the hex editor are
   done, game property watches included. Cheats (frozen addresses) wait on a question for
   the owner: with the engine owning the movie, a frozen value has to be
   written inside the engine's own replays too (a seek replays without
   the window), so it becomes a list the engine keeps.
4. **Lua's libraries** (memory, joypad, gui drawing, forms) as view model
   commands, so MCP has them too. Coordinate with the New-Features
   session, whose the MCP code is.
5. **Movie import, video recording, the debugger and trace logger.**
6. **Hotkeys and their configuration,** themes, recent files. All done
   but the hotkeys of what is not here yet: read-only (question 6),
   capturing the mouse, and the tools still missing. Then the Qt window is the default, and the C# one goes.

## What Chimera has that the prototype does not yet

Taken from Chimera's menus, dialogs and tools on 2026-10-23, and ticked off
as each lands here. Parts that wait on a question to the owner say so.

- TAStudio: Turbo seek (not needed: see the
  hotkeys row). Recording mode waits on
  question 6. (Chimera's Selection Undo and Redo are always greyed out
  and do nothing, so there is nothing of theirs to bring.)
- Tools: Encode Video's Capture OSD and Capture Lua.
  (The debugger has no disassembly or
  breakpoints for these cores in Chimera either: the engine has no call
  for them.) Cheats (frozen addresses) wait on question 5.
- Projects: the compile step (RPCS3's). The Cache Manager's greenzone
  budgets button and the unpacked cores and compiled code Chimera's
  lists (the prototype keeps neither). Recovery after a crash is done from
  snapshots alone: Chimera also writes every input to a journal file the
  moment it is made, through the engine, so its recovered work is never
  more than the last input old, where the prototype's can be five
  seconds old. The engine has that journal only for a movie log the
  window holds, not for a session's own movie; it needs a new engine
  export (question 7).
- Lua: where scripts run is question 4. Register To Text Editors (Sublime
  Text 2 completions) is not here, and the Lua Functions List has each
  function's parameters but not Chimera's descriptions of them.
- Not here by design for now: the MCP server (the New-Features
  session's work), Windows jump lists.

## Found in the engine on the way

- A state put back from the greenzone onto a machine far ahead of it can
  keep pages the state does not have (seen 2026-10-23, synthetic core:
  frame 0 put back from frame 30, then saved after a frame, carried two
  pages more than the history's own frame 1). Put back from a machine on
  the history's own line, it does not. Nothing desynced in that test, but
  a state put back should arguably be the whole machine; Chimera's own
  state history check would report it as bad data. The prototype's check
  puts frame 0 back twice before it walks, and says so.
- Chimera's TAStudio, and the engine's line reader, read a pasted line by
  place: a line copied with "include frame numbers when copying input"
  ("00002 |..U.....|") pastes its digits as buttons held (seen
  2026-10-23 in the engine's reader and in MovieController.SetFromMnemonic).
  The prototype refuses a pasted line that does not start with '|', as
  every line a movie writes does. Chimera's own paste could do the same.

- Chimera's "Run in background" box, when cleared, pauses the machine
  when the window loses focus only if "Pause when menu activated" is
  ticked too (MaybePauseFromMenuOpened returns early otherwise; seen
  2026-10-23 in MainForm.Events.cs). The prototype pauses whenever the
  box is cleared, as the box's own words say.

- Chimera's frame skip counter (Throttle.Step) leaves N-1 of every N
  frames undrawn for "skip N" (skip 1 skips nothing, and unthrottled,
  "3", draws one in three), and auto-minimized it counts below zero
  while the machine is on time, so later it skips nothing until a pause
  resets it (seen 2026-10-23, reading the code). The prototype skips N
  after each frame drawn, and auto-minimized only as many as it is
  behind.

## Yours to decide

1. **How Qt is obtained for Windows.** Here it is built from source with
   Ubuntu's mingw (posix threading), in 12 minutes, without the network
   module. The other way is Qt's own prebuilt Windows libraries, built
   with a different compiler.
2. **When this leaves the private branch:** a public branch, CI, a
   preview build beside the C# window.
3. **macOS:** there is no Mac here; CI runners are the only way to check
   it.
4. **Where Lua scripts run.** Here they run on the window's thread and
   hear of frames after the machine has moved on, so a script that sets
   the next frame's input (a bot) can miss frames, and every memory read
   waits for the machine's thread. Chimera's scripts run between frames on
   the one thread there is. The choices: scripts on the machine's thread,
   called before and after every frame, reaching the window's view models
   by waiting on the window (Chimera's behaviour, a little slower for
   scripts that drive windows); or scripts on the window's thread with a
   frame-by-frame lock step for those that ask for it.
5. **Frozen addresses (cheats).** With the engine owning the movie, a
   seek replays inside the engine without the window, so a frozen value
   has to be written there too: a list of frozen addresses the engine
   keeps and writes before every frame. Whether it is saved with the
   project, and whether a movie made with one says so, are the questions.
6. **Recording mode (read-only).** Chimera's TAStudio starts in play mode:
   the keys held while the machine runs are not written into the movie
   unless recording mode is on (the "Toggle read-only" hotkey, Q), and in
   recording mode a frame's input is replaced by what is held. The
   prototype has no such mode: what is held is always added to the frame
   run (a press it lacks becomes an edit), and past the end the movie
   grows. Whether to follow Chimera here - starting read-only, and
   replacing rather than adding in recording mode - changes how a person
   records, so it waits for the owner.
7. **An input journal for the session's movie.** Chimera keeps every
   input against a crash the moment it is entered: the engine appends it
   to a journal file (ce_movie_log_journal_open) on the movie log the C#
   window holds. Here the engine owns the movie inside the session, and
   the session has no such call, so the prototype keeps a snapshot every
   five seconds instead and a crash can lose up to five seconds of work.
   A session-level journal call (the session writing its own movie's
   changes to a file) would close that gap; it is a new engine export,
   so it is the owner's call.
