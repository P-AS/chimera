# macOS

Chimera runs on macOS on Apple silicon: the engine natively, and the interface
through XQuartz, with the game's picture in a window of its own. This is what
runs, what it needs, and why it is put together this way.

## What runs

- **The native libraries**, built by the same meson build as Linux's:
  libchimera, libminiboxhost, lua54, e_sqlite3, cimgui, zstd, SDL2 and
  OpenAL Soft (CoreAudio only). None of them links anything of Homebrew's.
  All of the engine's tests pass, the GPU bridge's among them.
- **The sandbox.** miniBox's `macos` branch: a PAL on Mach memory, SIGBUS
  faults, and RW/RX switching for guest pages the guest maps RWX. A core's
  aarch64 package runs as built for Linux - miniBox's aarch64 machine keeps
  the guest's thread pointer in memory and leaves x18 alone, because macOS
  keeps neither for it (miniBox MACHINE-SPEC, "aarch64").
- **The GPU bridge**, on a CGL context with no drawable (OpenGL 4.1 core).
- **Headless**: `chimera-run` (and `chimera-mcp` builds; the engine's MCP
  tests pass, its pipe included, but nothing has driven it here). The SRB2
  core's arm64 package plays through `chimera-run` here, records and replays
  a movie, and replays the same with a savestate round trip on every frame.

```sh
meson setup build/meson-macos --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-macos && meson install -C build/meson-macos
meson test -C build/meson-macos
build/dll/chimera-run <package> <rom> <movie> ...
```

Requirements: Xcode's command line tools, and Homebrew's meson, ninja and
cmake. A core's guest is built on Linux; miniBox's README says how to build
guests on macOS too (Homebrew's `aarch64-elf-gcc`).

## The interface

The interface is Windows Forms on .NET Framework 4.8, which Linux runs on
Mono. On macOS:

- **It builds.** Microsoft's .NET 8 SDK builds `source/gui/Chimera.sln` on
  macOS unchanged.
- **It runs under X11, through XQuartz.** Mono's own macOS driver for Windows
  Forms is Carbon, which was never ported to 64-bit ("The Carbon driver has
  not been ported to 64bits"), so the only one that works is X11's
  (`MONO_MWF_MAC_FORCE_X11=1`). That needs a libgdiplus built with X11, which
  Homebrew's is not (`--without-x11`); libgdiplus 6.2's configure turns X11
  off when `--with-x11` is given at all, so it is built without the option.
- **The picture is a window of its own** (user-decided, 2026-10-19). The
  display is an OpenGL 3.2 core context, and XQuartz's OpenGL (GLX) stops at
  2.1, so it cannot be drawn in the interface's X11 window. On macOS SDL uses
  its native driver (Cocoa) instead, and the display is a separate native
  window with OpenGL 4.1 core from the system. It follows the size and the
  visibility of the place it has in the interface, which says "The display is
  in its own window". See docs/design-principles.md.

Run it with the X11 libgdiplus first on the library path, Homebrew's
libraries only as a fallback (`DYLD_FALLBACK_LIBRARY_PATH`): Homebrew's
`libGL.dylib`, when there is one, is Mesa's, and must not stand in for the one
inside OpenGL.framework that the display and the GPU bridge use.

```sh
export DISPLAY="$(launchctl getenv DISPLAY)"      # XQuartz
export MONO_MWF_MAC_FORCE_X11=1 MONO_WINFORMS_XIM_STYLE=disabled
export DYLD_LIBRARY_PATH="<libgdiplus with X11>:$PWD/build/dll:$PWD/build"
export DYLD_FALLBACK_LIBRARY_PATH="/opt/homebrew/lib:/usr/local/lib:/usr/lib"
cd build && mono Chimera.exe
```

Played this way on an M1: SRB2 (its software and its `opengl-hw` renderers),
and PCSX2 with the software renderer, savestates included.
SRB2 runs at its full 35 frames a second there (34 measured at 1280x800),
as on Linux on the same machine.

What does not work yet:

- PCSX2's `opengl-hw` renderer: it needs GL_ARB_shading_language_420pack
  (OpenGL 4.2), and macOS stops at 4.1. Its `vulkan-hw` would need the Vulkan
  bridge on macOS, which is not begun. The software renderer plays the same
  run: no renderer changes the machine.
- The display window takes no mouse input, does not go full screen with the
  interface, and moves on its own; closing it does nothing.
- Two path helpers (`PathExtensions.IsSubfolderOf`, `GetRelativePath`) call
  GNU realpath (`-Lm`, `--relative-to`), which macOS's lacks; their tests fail
  here.
- The interface tests run under XQuartz until XQuartz itself stops ("Could
  not destroy window"), about 95 tests in, at a test that re-opens a context
  menu.
