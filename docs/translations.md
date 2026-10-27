# Translations

Chimera's window can be shown in other languages (issue 376). This document
says what can be translated, how Chimera picks a language, and how a language
is added. No language other than English is there yet.

## What is translated

The window's own words: every menu, label, button and message the window's
QML writes with `qsTr`. When this was written, `tools/update-translations.sh`
found 1425 of them in 39 windows.

Not translated: what the view models say (status lines, errors and the like,
written in C++ without Qt), what a core's package declares (its settings,
their descriptions, its controls), the Lua functions list, and the documents.

## How a language is picked

At start, Chimera goes through the person's system languages in order and
uses the first that has a translation built into the program
(`source/ui/qt/translations.cpp`). A more precise name is tried first, then
the general one: a Canadian French system takes `fr_CA`, else `fr`. With
none, the window stays in English. There is no setting to choose another
language than the system's.

## Adding a language

1. `tools/update-translations.sh <language>` (for example `fr`, `pt_BR`,
   `ja`) writes `translations/chimera_<language>.ts` with every word of the
   window. Run it again after the window changes: words already translated
   are kept.
2. Translate the file in Qt Linguist (`linguist
   translations/chimera_<language>.ts`) or any editor of `.ts` files.
3. Name the file in `chimera_ts` in `source/ui/meson.build`, and list its
   compiled form, `chimera_<language>.qm`, in `source/ui/qt/i18n.qrc` (made
   with the first language, under the prefix `/i18n`). The build compiles it
   with Qt's `lrelease` and builds it into the program.

The release builds its own Qt (`tools/build-qt-linux.sh` and the Windows
one) without Qt's tools for translations (the `qttools` module), so the
first language needs those added there as well.

## The test

`test-translations` (run by `meson test` where `lrelease` is found) builds a
French word of its own (`source/ui/qt/tests/chimera_fr.ts`) and checks that a
French system loads it and a German one loads nothing.
