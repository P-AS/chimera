# Translations

Chimera's window can be shown in other languages (issue 376). This document
says what is translated, how the language is chosen, and how a language is
added or corrected.

The window ships in English, German (`de`), Spanish (`es`), French (`fr`),
Italian (`it`), Japanese (`ja`), Portuguese (`pt`, written as in Brazil) and
Simplified Chinese (`zh_CN`). The translations were written in one go
(owner, 2026-10-28) and are meant to be corrected by people who speak the
language: a correction is an edit of one line in its `.ts` file.

## What is translated

The window's own words: every menu, label, button and message the window's
QML writes with `qsTr`. When this was written, `tools/update-translations.sh`
found 1428 of them (1163 different ones) in 39 windows.

Not translated: what the view models say (status lines, errors, the run's
own marker names and the like, written in C++ without Qt), what a core's
package declares (its settings, their descriptions, its controls), the Lua
functions list, and the documents.

## How the language is chosen

Config > Language lists English and every translation built into the
program, each in its own language. The window's words change at once.

The choice is remembered (`customize.language` in the window's settings).
At the first open, when nothing is remembered yet, the window takes the
first of the system's languages that it has a translation for - a more
precise name first, then the general one, so a Canadian French system
takes `fr` and a Brazilian one `pt` - and English when there is none. That
choice is then remembered too, so a later change of the system's language
does not change Chimera's (owner, 2026-10-28). A remembered language whose
translation is gone is chosen again the same way.

When the Language menu is in another language, its English name is shown
beside it ("Sprache (Language)"), so a person who picked a language they
cannot read can find the way back.

## Adding a language

1. `tools/update-translations.sh <language>` (for example `ko`, `pt_PT`)
   writes `source/ui/translations/chimera_<language>.ts` with every word of
   the window. Run it again after the window changes: words already
   translated are kept, and new ones are marked unfinished.
2. Translate the file in Qt Linguist (`linguist
   source/ui/translations/chimera_<language>.ts`) or any editor of `.ts`
   files. Keep each `%1`, `%2`, the `&` before a menu's shortcut letter (in
   Chinese and Japanese the letter goes in brackets after the word, as
   "(&F)"), and the spaces and line breaks at either end of a text.
3. List its compiled form in `source/ui/i18n.qrc`:
   `<file alias="chimera_<language>.qm">translations/chimera_<language>.qm</file>`.
   The build compiles it with Qt's `lrelease` and builds it into the
   program, and Config > Language lists it.

The build needs Qt's `lrelease`. The Qt the release ships is built without
Qt's tools, so the release build takes `lrelease` from the Qt it builds the
Windows window with (aqt's, `--host-qt` of `tools/build-bundle.sh`), for both
platforms.

## The test

`test-translations` checks the built-in languages (the list, a word in
Spanish and one in Japanese), how the first open picks one from the system's
languages (French for a Canadian French system, `zh_CN` for China, English
for Taiwan and Korea), that a remembered choice wins over the system's, and
that English puts the words back as written. It also builds a French test
word of its own (`source/ui/qt/tests/chimera_fr.ts`).
