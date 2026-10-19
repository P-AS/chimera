# macOS

Chimera's engine runs on macOS on Apple silicon; its interface does not yet.
This is where that stands and why.

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

## The interface: not yet

The interface is Windows Forms on .NET Framework 4.8, which Linux runs on
Mono. On macOS:

- **It builds.** Microsoft's .NET 8 SDK builds `source/gui/Chimera.sln` on
  macOS unchanged.
- **It starts, under X11 only.** Mono's own macOS driver for Windows Forms is
  Carbon, which was never ported to 64-bit ("The Carbon driver has not been
  ported to 64bits"), so the only one that works is X11's
  (`MONO_MWF_MAC_FORCE_X11=1`), through XQuartz. That needs a libgdiplus
  built with X11, which Homebrew's is not (`--without-x11`); libgdiplus 6.2's
  configure turns X11 off when `--with-x11` is given at all, so it is built
  without the option. With those, Chimera's own windows open and draw.
- **Its display does not.** The display is an OpenGL 3.2 core context that
  SDL2 makes on the frontend's own (X11) window. XQuartz's GLX offers
  OpenGL 2.1, so that context cannot be had, and Chimera stops at start with
  "This frontend requires OpenGL 3.2". No driver or package changes that.

What would get past it, none of it begun:

- render the display into a CGL context with no window (as the GPU bridge
  does) and draw each frame into the Windows Forms control from there - one
  window, a copy per frame;
- put the picture in a native window of its own (SDL2 on Cocoa) beside the
  X11 interface;
- an interface on a toolkit that is native on macOS.
