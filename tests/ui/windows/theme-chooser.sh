#!/bin/bash
# Asks, on WINDOWS, whether Config > Theme opens on the real toolkit with every
# shipped theme listed and draws its picture of Chimera in the theme picked.
# tests/ui/run-ui-tests.sh asks the same of Mono (ThemeChooserFormTests); this
# asks the toolkit Chimera's users run. See ThemeChooser.cs for what it checks.
#
# Run it from WSL on a Windows box:
#
#     dotnet build source/gui/Chimera.sln -c Release
#     ./tests/ui/windows/theme-chooser.sh
#
# It never touches an installed Chimera: everything is copied into a scratch
# folder of its own, which is removed afterwards. It writes a picture of the
# window with Aizome picked, and exits non-zero if a colour is wrong.
set -u

here="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$here/../../.." && pwd)"
build="$repo_root/build"
[ -f "$build/Chimera.exe" ] || { echo "no build/Chimera.exe - run: dotnet build source/gui/Chimera.sln -c Release" >&2; exit 1; }

csc="${CSC:-/mnt/c/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe}"
[ -x "$csc" ] || { echo "no csc.exe at $csc (set CSC=... to point at one)" >&2; exit 1; }

# a folder Windows can reach, because the program being compiled is a Windows one
win_temp="${WIN_TEMP:-/mnt/c/Users/$USER/AppData/Local/Temp}"
[ -d "$win_temp" ] || win_temp="$(dirname "$(mktemp -u)")"
work="$win_temp/chimera-theme-chooser"
rm -rf "$work"; mkdir -p "$work" || exit 1
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

# the frontend's assembly name is Chimera.Client.GUI, so it is copied under it
cp "$build/Chimera.exe" "$work/Chimera.Client.GUI.exe"
cp "$build/dll/"*.dll "$work/" 2>/dev/null
cp "$here/ThemeChooser.cs" "$work/"
# the themes from the repository, not build/: what ships is what is checked
mkdir -p "$work/Themes"
cp "$repo_root"/Themes/*.chimeraTheme.json "$work/Themes/"

if command -v wslpath >/dev/null; then winpath="$(wslpath -w "$work")"
else winpath="$(cd "$work" && cmd.exe /c cd 2>/dev/null | tr -d '\r')"
fi
[ -n "$winpath" ] || { echo "cannot work out the Windows path of $work" >&2; exit 1; }

"$csc" -nologo -target:exe -platform:x64 \
	-out:"$winpath\\ThemeChooser.exe" \
	-r:System.dll -r:System.Core.dll -r:System.Drawing.dll -r:System.Windows.Forms.dll \
	-r:"$winpath\\Chimera.Client.GUI.exe" -r:"$winpath\\Chimera.Client.Common.dll" \
	-r:"$winpath\\Chimera.Common.dll" -r:"$winpath\\Chimera.Emulation.Common.dll" \
	"$winpath\\ThemeChooser.cs" 2>&1 | tr -d '\r'
[ -f "$work/ThemeChooser.exe" ] || { echo "did not compile" >&2; exit 1; }

"$work/ThemeChooser.exe" "$winpath\\Themes" "$winpath\\theme-chooser-windows.png" 2>&1 | tr -d '\r'
status=${PIPESTATUS[0]}

out="${OUT_DIR:-$repo_root/tests/ui/shots}"
mkdir -p "$out"
cp "$work/theme-chooser-windows.png" "$out/" 2>/dev/null && echo "picture: $out/theme-chooser-windows.png"
exit "$status"
