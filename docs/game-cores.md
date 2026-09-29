# Game cores

A **game core** is one game rather than one machine: an open-source game
(written open, or reconstructed from the original) built as a Chimera core,
run by the same engine, sandbox, projects, movies and TAStudio as any other
core. SDLPoP (Prince of Persia) is the first. User-decided, 2026-09-28.

Two things set a game core apart from an emulator core, and nothing else
does:

1. It is a different **kind** of core, and every list of cores says so.
2. What the tools watch, poke and freeze is the game's own **properties** -
   the Kid's position, his hit points, the level, the random seed - by name,
   rather than addresses in a machine's memory.

Everything else is what a core already is. The game's data files are
**firmware**: the original files, which the package never carries (a port that
redistributes resources of its own does not use them). The game's options that
change play are **settings**, recorded in the project like a machine's. A
movie row is **one step of the game's logic**, and a lag frame is a step that
did not read the controls.

## The kind

`waterbox.config` carries `"kind": "game"`. Absent means `"emulator"`, which is
every core before this one. `official-cores.json` carries the same field for
the roster, so the Core Manager knows the kind of cores it has not downloaded
yet.

A list of cores is one list, the emulators first, with a choice above it
(user-decided, 2026-09-29):

- File > Core Manager: a Type column (Emulator or Game) and Show: All /
  Emulators / Games. A core the roster does not ship says "(added by hand)" in
  its Source. Select all and the bulk buttons act on the rows shown: a row the
  filter hides is unticked.
- Config > Firmware: Show: All / Emulators / Games, over the cores' groups.
- The new-project wizard: Kind: Emulator / Game above the core, which lists
  that kind only. A kind no installed core is cannot be chosen; the wizard
  opens on the last project's core when it has one, else on the kind of the
  last project made.
- The Cache Manager makes no distinction.

Each window remembers its choice (Config: `CoreManagerShows`,
`FirmwareShows`, `NewProjectKind`).

While a game core runs, the menu bar's Emulator menu is called **Game**
(user-decided, 2026-09-29): there is no emulator, and the menu holds the
game's own options (the timer below) beside what any core's menu holds
(firmware, the core's tools).

## Properties

A game core's properties are **a labelled memory domain** (user-decided): the
core keeps the properties in one block of its own memory, laid out as a packed
struct, exposes that block as an ordinary memory domain named `Game State`, and
describes it with a property table. So every tool that works on memory works
on properties unchanged - RAM Watch, RAM Search, the Hex Editor, freezes, Lua's
`memory.*` - and the table is what lets them show `Kid.X` rather than
`Game State:0x0C`.

The core copies the game's variables into the block after every step, and
copies the block back into the game before the next one. A poke is therefore a
write into the block, applied when the next step begins; a freeze is the same
write made before every step, which is exactly what a freeze on a machine's RAM
already is.

Where the game's own memory can be exposed as it is (a port that keeps a level
in one contiguous structure, say), the core exposes it as further domains
beside `Game State`, and the table may describe offsets in those too
(user-decided: raw memory, if available). Those are real memory, read and
written in place.

### The export

```c
// JSON; "" or absent for a core with no properties (every emulator core)
ECL_EXPORT const char *GetGameProperties(void);
```

```json
{
  "properties": [
    { "name": "Kid.X", "domain": "Game State", "offset": 0, "type": "u8",
      "group": "Kid", "description": "Horizontal position in the room, in game units" },
    { "name": "Kid.Direction", "domain": "Game State", "offset": 3, "type": "s8",
      "values": { "-1": "Left", "0": "Right" } },
    { "name": "Kid.Alive", "domain": "Game State", "offset": 5, "type": "bool", "writable": false },
    { "name": "Frame Count", "domain": "Game State", "offset": 8, "type": "u64" },
    { "name": "Level Name", "domain": "Game State", "offset": 16, "type": "string",
      "length": 12, "encoding": "ascii" },
    { "name": "Guards.X", "domain": "Game State", "offset": 32, "type": "s16",
      "count": 5, "stride": 20, "group": "Guards" },
    { "name": "Door Open", "domain": "Level", "offset": 700, "type": "u8", "bit": 3, "bits": 1 }
  ]
}
```

What a property is:

- `name`: unique, and what watches, freezes and scripts store - never the
  offset, so a core may reorder its block between versions. An array's
  elements are named by index from 0: `Guards.X[2]`.
- `domain`: a memory domain the core exposes.
- `offset`: in bytes, within that domain (of the first element, for an array).
- `type`:
  - `u8` `s8` `u16` `s16` `u32` `s32` `u64` `s64`: integers;
  - `f32` `f64`: IEEE floats;
  - `bool`: one byte, 0 false and anything else true;
  - `string`: text in `length` bytes, ended early by a NUL, in `encoding`
    (`ascii`, the default; `latin1`; `utf8`; `utf16le`). A string set longer
    than `length` is cut, and a shorter one NUL-padded;
  - `bytes`: `length` raw bytes, shown in hex.
- `count` (optional, default 1): an array of this many elements.
- `first` (optional, default 0): the number the first element is called by,
  for a game that counts from 1 - `"first": 1` makes the rooms of an array of
  24 `Room Links.Left[1]` to `[24]`, as the game numbers them.
- `stride` (optional, default the element's own size): bytes from one element
  to the next - larger for a field of an array of structures.
- `endian` (optional): `little` (the default) or `big`.
- `bit` and `bits` (optional, integer types only): a bit field - `bits` bits
  starting `bit` bits from the least significant end of the value. Setting
  one writes those bits and no others. Sign-extended for a signed type.
- `group` (optional): for listing (`Kid`, `Guard`, `Level`).
- `values` (optional, integer types): names for an enumeration's values,
  shown instead of the number and accepted in its place.
- `writable` (optional, default true): false for what the game derives each
  step and would overwrite (a poke would do nothing).
- `description` (optional): one line for a tooltip.

Beside `properties`, the table may name **the game's own timer**:
`"gameTimer": "Time.IGT Ms"` names one whole-number property (not an array,
not a bit field, no named values) that holds the time the game has counted, in
milliseconds - an in-game time, however the game defines it. The core does the
game's arithmetic; the engine only reads the number and writes it as
`mm:ss.mmm` (`ce_session_game_time_ms`, `ce_game_time_text`; minutes past 99
take more digits). A name that is not such a property is a problem the engine
reports, and the table works without the timer.

The type system is the engine's (libchimera, `ce_session_property_*`): it
reads the export once after `Init`, checks every property against the
domains the core really has, and leaves out what it cannot use - a domain that
is not there, a span past its end, a type it does not read, a name already
taken, a bit field that does not fit - saying why; the rest of the table
still works. The engine reads, writes, shows and parses every value, so a
watch, a poke, a freeze and a script all agree on what the bytes mean, and
anything linking the engine (a solver) gets the same properties. The frontend
lists, shows and forwards. Any core may export a table (the synth test core
does, naming gridWalker's RAM, so the witness can drive the whole path); a
game core is simply the kind that always should.

### In the tools

- RAM Watch: Watches > Add Game Properties lists the properties under their
  groups, an array element by element; each ticked element becomes a watch
  named after it. One that fits a 1-, 2- or 4-byte watch as it is (an
  integer, an f32 or a bool, not a bit field, without named values) is one of
  RAM Watch's own, with its display types and the numeric poke box. Anything
  else - 64 bits, an f64, text, bytes, a bit field, named values - is a
  property watch the engine reads: its Type column says `u64`, `string(12)`,
  `u8:1`; Poke takes a line of text the engine parses (a number, hex after
  0x, a value's name, true/false, text, hex bytes); it is known by its name,
  so it is not edited, and a watch file or a paste finds it again by name. A
  watch added any other way - New Watch, from RAM Search, from the Hex Editor
  - on the address an element starts at takes the element's name unless it
  has a note already.
- Freezes: a freeze is named after its watch, so a property frozen from RAM
  Watch, RAM Search or the Hex Editor is listed in the cheats by name. A
  property watch's freeze holds the value as the engine's text and sets it
  again before every step - whole, whatever its width - and one on a bit
  field touches no other bit. After the core is reloaded a frozen property is
  found again by name, and dropped when the core no longer has it.
- The game's timer: Game > Display Game Time (on by default, Game Time in the
  message positions, a hotkey to toggle) draws it on the screen as
  `IGT mm:ss.mmm`; the item is there only for a game whose core names a
  timer, as it is the game's option and not every core's. A saved project
  carries it at the end of the movie, for whoever reads the project later:
  `GameTimeMs` (the number), `GameTime` (as shown) and `GameTimeFrame` (the
  frame it was read at, the movie's length). They are written only when the
  machine has run to the end since the last edit before it, and removed
  otherwise, so a value that is there is the movie's; an edit before the end
  forgets it, as it forgets lag.
- RAM Search: an address that starts an element is listed with its name.
- Hex Editor: the title names the element the highlighted byte belongs to,
  and which of its bytes it is.

### Lua

`memory.*` works on `Game State` as on any domain. On top of it, a small
library by name:

- `game.list()`: the property names, in the core's order (an array once).
- `game.get(name)`: an integer, a float for `f32`/`f64`, a boolean for `bool`,
  a string, or a table of byte values for `bytes`. A `u64` comes back as the
  Lua integer with the same 64 bits (Lua's integers are signed). An array by
  its own name is a table of its elements, from 1 as Lua counts;
  `game.get("Guards.X[2]")` is one element, from 0 as the game counts.
- `game.set(name, value)`: the same kinds in, an array by its own name from a
  table; returns whether it was set. A number is taken when it fits the
  width as a signed or an unsigned value (-1 sets every bit of a `u64`) and
  refused otherwise, rather than wrapped; `bytes` takes a table of exactly
  its length.
- `game.describe(name)`: name, domain, offset, type, size, count, stride,
  endian, encoding, bit, bits, group, writable, description, and label (the
  value as the core names it).

A name the core does not have is not an error: `get` and `describe` return
nil, `set` returns false, and the console says why - the way `memory.*`
treats a domain it does not know. (An exception thrown back through Lua after
a script has yielded a frame takes the process down under Mono, even inside a
`pcall`; found while writing the witness leg.)

## What a movie row is

One step of the game's logic: one pass of the game's main loop in which it
reads its controls and moves the world on - not a 60 Hz redraw. A step is as
long as the game makes it. In Prince of Persia a step of play is 1/12 s
walking and 1/10 s with the sword drawn; the title, the cutscenes and a pause
read the controls every 1/60 s, so their steps are 1/60 s; a room change adds
a dark 1/10 s step.

- `GetVsyncNumerator`/`GetVsyncDenominator` report the rate of the step just
  run. The engine asks after every shown frame (ce_session_vsync_*), so the
  frontend paces each step at its own length; a seek's unshown frames do not
  ask.
- `samplesPerFrame` is a hard cap on the audio of one step, not a typical
  count: a core whose steps vary declares its longest (SDLPoP declares 44100,
  a second).

## Settings and data

- The game's original data files are firmware, each with its hash, and the
  core itself checks it was given the version it plays (Prince of Persia 1.0,
  1.1, 1.3 and 1.4 differ; SDLPoP's version setting picks one) and refuses any
  other by name: a file given with a
  mismatched hash is only a warning in the frontend, and chimera-run's
  `--firmware` is not hash-checked at all.
- A game core declares its project slots like any core, at `min` 0 when the
  project needs no file of its own - the game is all firmware. SDLPoP's are a
  custom level set and a savestate to start from. A core that takes no file
  at all declares an empty list (`"slots": []`, SDLPoP2's): the wizard's files
  page then says so and asks nothing (until 2026-09-29 the wizard wanted at
  least one slot declared).
- Options that change play - a port's fixes and enhancements, difficulty - are
  settings, part of the machine. Options that change only the picture or the
  sound are not.
- A mod or a custom level set is a project file in a slot of its own.
