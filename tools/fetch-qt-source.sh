#!/bin/sh
# Downloads the Qt 6.12.0 source Chimera's window is built from - qtbase,
# qtshadertools, qtdeclarative and qtsvg, Qt's own "submodules" archives -
# into a folder, each checked against the hash written here, for
# tools/build-qt-linux.sh and tools/build-qt-windows.sh. A CI run does this;
# Chimera itself downloads nothing.
#
#   tools/fetch-qt-source.sh <folder>
#
# An archive already there with the right hash is kept.
set -eu
dir=$1
version=6.12.0
base="https://download.qt.io/official_releases/qt/6.12/$version/submodules"
mkdir -p "$dir"
while read -r module sha; do
	file="$dir/$module.tar.xz"
	if [ -f "$file" ] && echo "$sha  $file" | sha256sum -c --quiet - 2>/dev/null; then
		continue
	fi
	curl -fsSL --retry 3 -o "$file.part" "$base/$module-everywhere-src-$version.tar.xz"
	echo "$sha  $file.part" | sha256sum -c --quiet - || { echo "$module: not the archive Chimera is built from" >&2; rm -f "$file.part"; exit 1; }
	mv "$file.part" "$file"
done <<EOF
qtbase a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307
qtshadertools c7d84f436e1aaef39fdcadebf2bd71bdf24dc69497e13f2e0198873cbbfea2ab
qtdeclarative 311f3a2603e1973bb59baef9dfa740a376de713157d4782ee043681e889c9260
qtsvg e4ab39534ec97987b1b9b60ef7f4d3253d5912a69f8c713a01753174a4029331
EOF
echo "Qt $version source in $dir"
