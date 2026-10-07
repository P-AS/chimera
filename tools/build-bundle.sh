#!/bin/bash
# Assembles a ready-to-run Chimera bundle: the frontend, its natives and the
# licences.
#
# It carries NO CORES, and Chimera downloads none (docs/core-manager.md). They
# are other repositories that build, package and publish themselves; a user
# downloads a core's package from its project, or builds it, and puts it in the
# bundle's Cores/ folder, which ships empty. Building them all here made the
# bundle large and the release slow, and bumping any one of them rebuilt the
# world.
#
# What is lost with them is "one chimera commit pins one exact bundle". What
# replaces it: a movie cites the exact core package that recorded it, by
# version and SHA-1, which is what somebody replaying it has to find.
#
# Usage: tools/build-bundle.sh --platform windows|linux --out <dir>
#                              [--skip-natives]
#
#   --skip-natives   the natives and the managed solution are already built
#                    (a rebuild of the same platform)
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
platform=""
out=""
skip_natives=0
while [ $# -gt 0 ]; do
	case "$1" in
		--platform) platform="$2"; shift 2 ;;
		--out) out="$2"; shift 2 ;;
		--skip-natives) skip_natives=1; shift ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ -n "$platform" ] && [ -n "$out" ] || { echo "usage: build-bundle.sh --platform windows|linux --out <dir>" >&2; exit 2; }
case "$platform" in
	windows|linux) ;;
	*) echo "platform must be windows or linux" >&2; exit 2 ;;
esac

say() { printf "\n== %s\n" "$1"; }

# Every core package builds its guest against a miniBox guest kit; default it to
# the submodule the frontend itself is built against, or a stale sibling clone
# silently produces a core.wbx built against a different ABI.
export MINIBOX_DIR="${MINIBOX_DIR:-$root/extern/chimera-common-minibox}"

native_dir="$root/build/meson-$platform"
if [ "$skip_natives" -eq 0 ]; then
	say "native dependencies ($platform)"
	if [ ! -f "$native_dir/build.ninja" ]; then
		if [ "$platform" = "windows" ]; then
			meson setup "$native_dir" --prefix "$root/build" --libdir dll \
				--cross-file "$root/extern/meson/mingw-w64.ini"
		else
			meson setup "$native_dir" --prefix "$root/build" --libdir dll
		fi
	fi
	meson compile -C "$native_dir"
	# the Linux build installs into build/dll, which is also where the managed
	# build looks for its native neighbours
	[ "$platform" = "linux" ] && meson install -C "$native_dir"

	say "managed solution"
	dotnet build "$root/source/gui/Chimera.sln" -c Release /nodeReuse:false -p:UseSharedCompilation=false -v q --nologo
fi

say "staging into $out"
rm -rf "$out"
mkdir -p "$out/dll" "$out/Cores" "$out/Lua" "$out/Tools"
cp "$root/build/Chimera.exe" "$root/build/Chimera.exe.config" "$root/build/Chimera.xml" "$out/" 2>/dev/null || true
cp "$root"/build/dll/*.dll "$out/dll/" 2>/dev/null || true   # managed assemblies (IL, portable)
cp -r "$root"/build/Lua/. "$out/Lua/" 2>/dev/null || true
cp -r "$root"/build/Tools/. "$out/Tools/" 2>/dev/null || true

if [ "$platform" = "windows" ]; then
	cp "$native_dir"/*.dll "$out/dll/"
else
	cp "$root"/build/dll/*.so "$out/dll/" 2>/dev/null || true
	cp "$root/build/ChimeraMono.sh" "$out/" 2>/dev/null || true
fi

# No list of cores goes in: Chimera downloads nothing and knows of no core it
# has not been given (docs/core-manager.md). Cores/ ships empty, and what a user
# puts in it is what there is.

# ffmpeg is part of the bundle, not something the frontend asks a person to go
# and find the first time they encode a video.
say "ffmpeg"
"$root/tools/fetch-ffmpeg.sh" "$platform" "$out/dll"

say "licences"
# What the bundle may be used for. It carries NO cores - each comes from its own
# project, put in Cores/ by whoever wants it, and brings its own terms with it,
# several of which (Genesis Plus GX, Opera, Snes9x) forbid commercial use and
# bind whatever they are installed into. This states the frontend's own terms
# and says where the rest come from.
python3 "$root/tools/bundle-licenses.py" "$out" --chimera-root "$root"

say "build stamp"
# Every "it failed" report is only as useful as knowing WHICH build failed. This
# names the exact commits and the exact package hashes.
{
	printf "Chimera %s bundle\n" "$platform"
	printf "chimera:   %s %s\n" \
		"$(git -C "$root" rev-parse HEAD)" \
		"$(git -C "$root" log -1 --format=%s | cut -c1-60)"
	printf "\nguest kit:\n"
	git -C "$root" submodule status extern/chimera-common-minibox 2>/dev/null | sed 's/^/  /'
	printf "\nfiles (sha1):\n"
	( cd "$out" && sha1sum Chimera.exe 2>/dev/null | sed 's/^/  /' )
	printf "\ncores: none, and Chimera downloads none. Get a core's package from its\n"
	printf "project and put it in Cores/; a movie names the exact package that recorded it.\n"
} > "$out/BUILD.txt"
cat "$out/BUILD.txt"

say "verifying"
# A truncated dll is indistinguishable from a bug until you check.
bad=0
for f in "$out/Chimera.exe" "$out"/dll/*; do
	[ -s "$f" ] || { echo "EMPTY: $f" >&2; bad=1; }
done
# The bundle normally has no cores at all; one is here only if somebody put it
# there, and then it had better be a readable package.
for z in "$out"/Cores/*.chimeraCore; do
	[ -e "$z" ] || break
	python3 -c "import sys,zipfile; zipfile.ZipFile(sys.argv[1]).testzip()" "$z" || { echo "CORRUPT: $z" >&2; bad=1; }
done
[ -s "$out/LICENSES.md" ] || { echo "MISSING: LICENSES.md" >&2; bad=1; }
[ -d "$out/Cores" ] || { echo "MISSING: Cores/ (the folder a user puts core packages in)" >&2; bad=1; }
[ ! -e "$out/official-cores.json" ] || { echo "PRESENT: official-cores.json (the bundle carries no list of cores)" >&2; bad=1; }
# Chimera reaches for nothing over the network, so nothing that could is shipped
if find "$out/Lua" -type f | grep -q .; then echo "PRESENT: files under Lua/ (the bundle ships no Lua modules)" >&2; bad=1; fi
[ "$bad" -eq 0 ] || { echo "  the bundle is NOT clean" >&2; exit 1; }
printf "  %s in %s\n" "$(du -sh "$out" | cut -f1)" "$out"
