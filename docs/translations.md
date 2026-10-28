# Translations

Chimera can be shown in other languages. This document says how its texts
are kept, how the language is chosen, and how a language is added or
corrected.

## The rules

The owner set these on 2026-10-28:

1. Every text Chimera shows a person is a named key (for example
   `common.file`) whose words are in a language table. English is one of
   the tables.
2. No text is written into the code itself.
3. Every language Chimera has gives every key.
4. The same holds for the cores, their settings included: each core
   package carries tables of its own, one per language.

What counts is what a person reads: the window, its messages and errors,
and what a core declares. These are not translated: the logs written for
developers (the core log, standard error), names in file formats, the
descriptions of Lua functions, of MCP tools and of view-model properties
(they describe English function names), chimera-run's own output (a tool
for tests), control names such as `P1 Up` (identifiers that movies, key
bindings and scripts use), and names that are data rather than writing:
game titles, MAME's machine names, the community's patch and cheat names.

## The tables

`languages/<code>.json`, one file per language, `en.json` among them:

```json
{
	"name": "Deutsch",
	"english": "German",
	"texts": {
		"common.file": "&Datei",
		"display_config.framerate_hz": "Bildrate: %1 Hz",
		"main.frozen_addresses": { "one": "%n Adresse", "other": "%n Adressen" }
	}
}
```

- `name` is the language's name in itself; `english` its name in English.
- A key is lowercase words joined by `_`, after the window or the subject
  they belong to and a dot. `common.` holds texts many windows share.
- `%1` to `%9` are filled in by the program. A text that depends on a
  number has one form per plural category of its language (`one`, `few`,
  `many`, `other`, as the CLDR names them), and `%n` is that number.
- `&` before a letter marks a menu's shortcut letter; `&&` is an `&`.

The build copies the tables beside the engine (`languages/` in the build
folder, `lib/languages/` in a bundle), and the engine reads them when the
program runs. The window asks for a key with `qsTrId("key")`, which the
engine answers from the table in use; the engine and the view models ask
through `ce_text`. A key that a table lacks shows as the key itself, so a
missing one is seen.

## How the language is chosen

Config > Language lists every table there is, each language named in
itself. The window's texts change at once.

The choice is remembered (`customize.language` in the window's settings).
At the first open, when nothing is remembered yet, the window takes the
first of the system's languages it has a table for - a more precise name
first, then the general one, so a Canadian French system takes `fr` and a
Brazilian one `pt` - and English when there is none. That choice is then
remembered too, so a later change of the system's language does not change
Chimera's (owner, 2026-10-28). A remembered language whose table is gone is
chosen again the same way.

In every language but English the Language menu's own name carries
"(Language)" beside it, so a person who picked a language they cannot read
can find the way back.

## Adding or correcting a language

A correction is an edit of one text in that language's file.

A language is added as `languages/<code>.json` with every key `en.json`
has, and its code in `languages/meson.build`. The engine needs the
language's plural rule (`pluralForms` and `pluralForm` in
`source/engine/source/language.cpp`).

## The test

`engine_language` checks every table against English: the same keys and
no others, the same `%1`..`%9`, the plural forms the language takes, a
menu's shortcut letter kept, and the spaces and line breaks at either end
kept. It also checks the plural rules, the filling of `%1`..`%9`, a
language used and another in its place, and the C calls. `translations`
checks the window's side: the language it starts in for a given system, a
remembered choice, and what `qsTrId` answers.
