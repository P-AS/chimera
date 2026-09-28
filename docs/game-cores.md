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
the roster, so the Core Manager can divide cores it has not downloaded yet.

Every list of cores - the Core Manager, the Cache Manager, the new-project
wizard, the precompiled-modules window - shows emulator cores, then a divider
row, then game cores. (A row rather than a ListView group: Mono's ListView
ignores groups in Details view, which is why the Core Manager's "External
cores" divide is a row too.)

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
    { "name": "Level", "domain": "Game State", "offset": 40, "type": "u16" },
    { "name": "Random Seed", "domain": "Game State", "offset": 44, "type": "u32" },
    { "name": "Kid.Alive", "domain": "Game State", "offset": 5, "type": "s8", "writable": false }
  ]
}
```

- `name`: unique, and what watches, freezes and scripts store - never the
  offset, so a core may reorder its block between versions.
- `domain`: a memory domain the core exposes.
- `offset`: in bytes, within that domain.
- `type`: `u8` `s8` `u16` `s16` `u32` `s32` `f32` `bool` (one byte, 0 or 1);
  little-endian. Nothing wider: the watch tools read at most 32 bits, and a
  wider value is two properties (`Score.Low`, `Score.High`).
- `group` (optional): for listing (`Kid`, `Guard`, `Level`).
- `values` (optional): names for an enumeration's values, shown instead of the
  number.
- `writable` (optional, default true): false for what the game derives each
  step and would overwrite (a poke would do nothing).
- `description` (optional): one line for a tooltip.

The engine reads the export once, after `Init`, and hands the text to the
frontend as `ce_session_game_properties`. It interprets nothing in it. The
frontend checks each property against the domains the core really has and
leaves out what it cannot use - a domain that is not there, an offset past its
end, a type it does not read, a name already taken - saying so on stderr; the
rest of the table still works. Any core may export a table (the synth test
core does, naming gridWalker's RAM, so the witness can drive the whole path);
a game core is simply the kind that always should.

### In the tools

- RAM Watch: Watches > Add Game Properties lists the properties under their
  groups; each ticked one becomes a watch of its own width and sign, named
  after it. A watch added any other way - New Watch, from RAM Search, from the
  Hex Editor - on the address a property starts at takes the property's name
  unless it has a note already.
- Freezes: a freeze is named after its watch, so a property frozen from RAM
  Watch, RAM Search or the Hex Editor is listed in the cheats by name.
- RAM Search: an address that starts a property is listed with its name.
- Hex Editor: the title names the property the highlighted byte belongs to,
  and which of its bytes it is.

### Lua

`memory.*` works on `Game State` as on any domain. On top of it, a small
library by name: `game.list()` (the names, in the core's order),
`game.get(name)` (a whole number, a float for `f32`, a boolean for `bool`),
`game.set(name, value)` (returns whether it was set) and `game.describe(name)`.
A name the core does not have is not an error: `get` and `describe` return
nil, `set` returns false, and the console says why - the way `memory.*`
treats a domain it does not know. (An exception thrown back through Lua after
a script has yielded a frame takes the process down under Mono, even inside a
`pcall`; found while writing the witness leg.)

## What a movie row is

One step of the game's logic: one pass of the game's main loop in which it
reads its controls and moves the world on. For Prince of Persia that is a game
tick (12 per second in play, faster in some cutscenes), not a 60 Hz redraw.
`GetVsyncNumerator`/`GetVsyncDenominator` report the rate of steps now.

## Settings and data

- The game's original data files are firmware, each with its hash, and a core
  checks it was given the version it plays (Prince of Persia 1.0, 1.3 and 1.4
  differ).
- Options that change play - a port's fixes and enhancements, difficulty - are
  settings, part of the machine. Options that change only the picture or the
  sound are not.
- A mod or a custom level set is a project file in a slot of its own.
