# An MCP server for Chimera (design)

Status: the design was decided by the owner on 2026-10-12 (the decisions are
listed at the end). The work is in progress; this section says what is built.

Built so far:

- One engine call opens a project (step 1 of the order of work).
- The engine's part of the server: the local pipe, the protocol, the log, and
  `chimera-mcp`, the program an agent starts, on Linux and Windows.
- In the window: Tools > MCP Server... with start, stop, the state, the tick
  box, the set-up command and the log; the title bar says "MCP server
  started" while it is.
- The tools that look: `status`, `controls`, `get_input`, `screenshot`,
  `list_memory`, `read_memory`, `registers`, `list_properties`,
  `get_property`, `list_branches`, `list_markers`.
- RAM search, the agent's own: `search_start`, `search_filter`,
  `search_results`, `search_undo`.
- The tools that change things, refused until the tick box is ticked: `seek`,
  `set_input`, `set_buttons`, `insert_frames`, `delete_frames`, `undo`,
  `save_branch`, `load_branch`, `add_marker`, `write_memory`, `set_property`,
  `set_register`. A branch of the movie as it was is saved before the first
  of them.
- Two command-line flags for runs nobody clicks in: `--mcp-server` and
  `--mcp-allow-changes`.

- `chimera-mcp` is in the Linux and Windows release bundles, in `dll/`, where
  the dialog's set-up command points.

Not built yet: `watch`, `advance_until`, and the windowless mode.

On Windows: the same test that edits a movie in the window was run against
the Windows build of Chimera, started with no window shown, and passed. The
dialog itself has not been looked at on Windows; only its Linux picture has.

## What it is

A way for an AI agent to work on the movie a person has open in Chimera: look
at the input, the picture and the memory, search RAM, and, if the person
allows it, edit the input. The person starts it from **Tools > MCP Server...**
in the running window and can stop it at any time.

MCP (Model Context Protocol) is an open standard for this. A program that
follows it (a "server") declares a list of tools, each with a name, a
description and typed parameters. An agent's own program (Claude Code, Claude
Desktop, Cursor and others) reads the list, and the model calls the tools and
gets structured answers back, pictures included.

There are two ways to use it, built in this order:

1. **In the window** (the main use). A person is working on a movie and asks
   their agent to analyse or change it. The agent works on the project that
   is open, and the person sees every step in TAStudio.
2. **Without a window.** An agent opens its own machine, with no display, to
   explore a game or check a movie. Same tools, plus opening and saving.

## How the agent reaches Chimera

An agent's program connects to an MCP server by starting it as a child
process and exchanging lines of JSON over that process's standard input and
output. A running Chimera window cannot be started by the agent, so there is a
small program in between:

```
agent's program  <-- standard input/output -->  chimera-mcp  <-- local pipe -->  Chimera window
```

- `chimera-mcp` is a small C++ program installed in `dll/` beside
  `chimera-run`. The agent's program starts it. It passes each request to the
  window and each answer back.
- The local pipe is a named pipe on Windows and a pair of FIFO files in the
  user's runtime folder on Linux, both open to the same user only. No socket
  call is used anywhere.
- When the server in the window is stopped, or no Chimera is running,
  `chimera-mcp` still lists the tools, and every call answers "Chimera's MCP
  server is not started: Tools > MCP Server... > Start". The agent's program
  therefore does not need restarting when Chimera is.

Nothing here opens a socket, listens on a port or resolves a name, so the rule
that Chimera has no network code (docs/core-manager.md, "Nothing reaches the
network") holds and `tools/check-no-network.sh` passes unchanged. MCP's other
way to connect, over HTTP, is not offered, for the same rule.

A second Chimera of the same user can serve beside the first under another
name: the environment variable `CHIMERA_MCP_NAME`, set to the same word for
that Chimera and for its `chimera-mcp`.

A user sets it up once in their agent. The dialog shows the line to copy, for
example for Claude Code:

```sh
claude mcp add chimera -- /path/to/chimera/dll/chimera-mcp
```

## Tools > MCP Server...

One dialog, usable at any time while Chimera runs:

- **Start / Stop.** The server always starts stopped when Chimera starts; its
  state is never remembered. Stopping it ends any call in progress with an
  error to the agent.
- **State.** Stopped, waiting for an agent, or connected, with the name and
  version the agent's program gives.
- **Allow the agent to make changes.** A tick box, off at first every time.
  Unticked, the agent can only look. Ticked, it may edit input, move the
  playback position and write memory.
- **The log.** One line per call: time, tool, arguments (shortened), result
  or error, how long it took. It can be saved to a file.
- **The set-up line** to paste into the agent, with a button to copy it.

While the server is started the main window's title says "MCP server
started" and the menu item is ticked, so it cannot be left on unnoticed.
Closing the dialog does not stop the server; closing Chimera does.

Chimera started with `--mcp-server` starts the server at launch, as pressing
Start does, and `--mcp-allow-changes` beside it ticks the box. They exist for
runs nobody clicks in (the tests use them); nothing else starts the server or
allows changes, and no setting in a configuration file can.

## What protects the person's work

- With the tick box off, every tool that changes anything is refused, and the
  refusal says which tick box allows it.
- Every edit the agent makes is an ordinary TAStudio edit: it appears in the
  piano roll and in the undo history, and can be undone.
- Before the agent's first change after the server is started, a branch of
  the movie as it was is saved, with a note saying so and the time. If the
  branch cannot be saved, nothing is changed: every change is refused until
  the server is stopped and started again. The engine holds this rule, not
  the window, so no path around the window's code skips it.
- The agent cannot save, close or open a project in the window, and has no
  access to files there. Saving stays the person's act.
- A memory write is not input: a movie cannot reproduce it. It is reported in
  the log and in `status` from the first write on, with the frame.

## The tools

Names and parameters are a sketch until built. "Changes" marks the tools the
tick box governs.

Looking:

| Tool | What it does |
| --- | --- |
| `status` | Core, game, frame, movie length, picture size, frame rate, whether changes are allowed, whether the core has stopped. |
| `controls` | The machine's buttons and axes by name, with each axis's rest value. |
| `get_input` | The movie's input over a range of frames. |
| `screenshot` | The picture of the current frame as a PNG, optionally scaled down. |
| `list_memory` | The memory domains and address buses, with sizes. |
| `read_memory` | Bytes or typed values at an address. |
| `registers` | The CPU registers, where the core gives them. |
| `list_properties` / `get_property` | A game core's named values (docs/game-cores.md). |
| `list_branches` / `list_markers` | The project's branches and markers. |
| `watch` | Names some addresses or properties; `seek` and `advance_until` then return their values every time. |

RAM search (the agent's own search, separate from the RAM Search window):

| Tool | What it does |
| --- | --- |
| `search_start` | Starts a search over a domain with a value size of 1, 2 or 4 bytes. Every address is a candidate and its value is remembered. |
| `search_filter` | Keeps the candidates whose value now is equal to, different from, greater or less than a given value, or than the value remembered. Then remembers the value now. |
| `search_results` | The candidates left, lowest address first, with the value now and the one remembered, up to a limit. |
| `search_undo` | Takes the last filter back. |

The way it is used: start a search, have the game change the thing looked for
(`seek` to another frame, or the person plays), filter for "different" or
"greater", and repeat until few addresses are left. It only reads, so it
needs no permission. One search at a time; it ends when the project is
closed or the server is stopped.

Changes:

| Tool | What it does |
| --- | --- |
| `seek` | Moves the playback position to a frame, back or forward, and answers when the machine is there. |
| `set_input` | Replaces the input from a frame on, one line a frame as `get_input` returns them. Past the movie's end it adds frames. |
| `set_buttons` | Presses or releases buttons, and sets axes, by name over a range of frames. Controls not named keep what they had. |
| `insert_frames` / `delete_frames` | Changes the movie's length at a frame. |
| `undo` | Undoes the last step of the undo history, the same as Edit > Undo. |
| `save_branch` / `load_branch` | Keeps or restores a branch. |
| `add_marker` | Places a marker with a text. |
| `write_memory` / `set_property` / `set_register` | Writes to the machine. Reported as said above. |

Each call that edits the input is one step in TAStudio's undo history, named
"Agent: ...", so a person can undo an agent's edit as they undo their own.

Not built yet: `advance_until` (play forward until a memory value meets a
condition).

Without a window only (second part of the work):

| Tool | What it does |
| --- | --- |
| `list_cores` / `describe_core` | The packages in the Cores folder and what each declares. |
| `open_project` / `open_game` | Opens a project, or starts one from a core and game files. |
| `save_project` / `export_movie` | Writes the project or the input as a movie file. |
| `close` | Closes the machine. |

## Where the code lives

- **In the engine (C++):** the protocol, the pipe, the list of tools, the log,
  and every tool that only needs the machine: memory, registers, properties,
  pictures, RAM search, conditions. This part is the same with and without a
  window.
- **In the interface (C#):** the dialog, and the tools that must go through
  TAStudio so the person sees them and can undo them: input edits, seeking,
  branches, markers. They call the same editing functions the piano roll
  itself calls.
- **How a call runs in the window.** A thread in the engine reads the pipe.
  The window's main loop asks the engine once per pass whether a call is
  waiting; the engine answers the calls it can itself and hands the others to
  the interface. So every call runs on the thread that runs the machine,
  between two frames, never beside it. If the window is busy (a dialog is
  open), the call waits, and is answered "Chimera is busy" after a time limit.
- **Nothing system-specific.** No tool names a system, a control or a core.
  Names are whatever the opened core declares.

## One place that opens a project

The steps from a project to a running machine (check that every file is found
and matches its hash, check the core against the one the project names, check
the files against the core's slots, build the list of files the core sees,
start the machine with the project's settings) are written twice today: in
`chimera-run.cpp` and in the C# interface (`RomLoader.LoadProjectGame`). The
server would have been a third.

They are now one engine call, `ce_session_open_project` (engine.h;
docs/project.md, "One place starts the machine"). `chimera-run` and the
window use it, the two copies are gone, and the server will use it too.

The same will be done for finding firmware in the user's Firmware folder when
the windowless mode needs it; today only the interface knows how.

## Rules of the design

- **Few round trips.** Every call costs the agent time and money. That is
  why ranges are taken in one call, why `advance_until` and `watch` exist,
  and why a picture is sent only when asked for.
- **Small answers.** Memory reads and search results have a size limit and
  say when they were cut. Pictures can be scaled down.
- **Editing the past behaves as it does by hand.** Changing input before the
  current frame drops the saved states after it, as any TAStudio edit does.
- **Errors are the engine's own words.** A core that stopped, a refused
  write, a frame that does not exist are tool errors with the engine's text.

## Which files the windowless mode may touch

In the window the agent has no file access. Without a window it must open and
save, so the limits are set when `chimera-mcp` is started:

- core packages are read only from the `--cores` folder;
- games, projects, movies and firmware are read only from the folders named
  with `--files <dir>` (repeatable) and from the work folder;
- everything is written inside the work folder, `--work <dir>`;
- any other path is refused;
- a project's files are checked against the hashes it records, and a mismatch
  is an error, since there is no person to ask.

It downloads nothing. Games and firmware are the user's own files.

## How it is tested

- The shared project-opening call: the existing tests must give the same
  results as before, since `chimera-run` and the window both open every
  project through it. That includes the test that runs the whole product
  with the synthetic core (`tests/synth/`) and the interface tests.
- The server in the window, in the interface tests: start it, connect as an
  agent's program would, read input and memory and compare with what the
  window itself shows; with the tick box off an edit is refused; with it on
  an edit appears in the piano roll, a branch was saved first, and undo
  removes it; stopping the server ends the connection.
- The windowless mode: play a known movie through the tools and compare the
  memory, byte for byte, with what `chimera-run` produces for it.
- Each check is broken on purpose once and seen to fail (docs/gates.md).
- Linux and the Windows build both.

## Order of work

1. One engine call that opens a project, used by `chimera-run` and the
   window.
2. The engine part of the server: protocol, pipe, log, and the tools that
   only look. `chimera-mcp` as the program in between. Tools > MCP Server...
   with start, stop, state and log.
3. The tools that change things, the tick box, the saved branch, undo.
4. RAM search, `watch`, `advance_until`.
5. The windowless mode and its file limits.
6. Release bundles, README, a note for the website.

The owner changed the order on 2026-10-12, once the window mode was complete
and tested on both systems: `chimera-mcp` ships in the release bundles now,
and the convenience tools and the windowless mode follow.

## Not planned

- Sound, video encoding, running Lua scripts through the server, CPU traces.
- More than one agent connected at once, or one agent on several Chimera
  windows.
- Any connection that is not local to the machine and the user.

## What was decided (owner, 2026-10-12)

1. A program an agent starts and talks to over standard input and output is
   inside the "no network code" rule.
2. `chimera-mcp` ships in the release bundles once it is tested, not before.
3. Opening a project is one piece of C++ in the engine that `chimera-run`,
   the window and the server all use, done now and not postponed. Avoiding
   repeated code comes first.
4. Memory writes are offered, and reported from the first one on.
5. Without a window, files are limited to the folders named at start.
6. Working inside the running window is the main use: Tools > MCP Server...,
   where the server can be started, watched, logged and stopped at any time.
7. The window comes first; the windowless mode follows.
8. In the window, changes are behind a tick box that is off at first.
9. The server always starts stopped; its state is not remembered.
