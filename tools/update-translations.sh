#!/bin/sh
# Makes or updates source/ui/translations/chimera_<language>.ts for each language named,
# from the window's own words: every qsTr in its QML and C++ (docs/translations.md).
# A translator then fills it in with Qt Linguist; words already translated are kept.
#
#   tools/update-translations.sh <language>...     for example: fr pt_BR ja
#
# Needs Qt's lupdate (LUPDATE=<path> when it is not on the PATH).
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
lupdate="${LUPDATE:-lupdate}"
[ $# -gt 0 ] || { echo "usage: $0 <language>..." >&2; exit 2; }
mkdir -p "$root/source/ui/translations"
for language in "$@"; do
	"$lupdate" -silent -locations relative -extensions qml,cpp,js "$root/source/ui/qt" -ts "$root/source/ui/translations/chimera_$language.ts"
	echo "source/ui/translations/chimera_$language.ts"
done
