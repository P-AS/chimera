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
- **The GPU bridges.** OpenGL on zink, Mesa's OpenGL over Vulkan, when
  Mesa is installed, and otherwise on Apple's CGL with no drawable (OpenGL
  4.1 core). Vulkan on whatever Vulkan driver the machine has: Mesa's
  KosmicKrisp, or MoltenVK. See "Mesa" below.
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
  window with OpenGL 4.1 core from the system. The interface window is only
  its menu and status bar: it keeps its size when a game changes the size of
  its picture, and shows nothing in the picture's place. The picture's window
  takes the picture's size the way the interface window does on other
  systems, and can be resized by hand; what is drawn follows it. See
  docs/design-principles.md.

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

- Without Mesa (below), PCSX2's `opengl-hw` renderer: it needs
  GL_ARB_shading_language_420pack (OpenGL 4.2), and Apple's OpenGL stops at
  4.1. The software renderer plays the same run: no renderer changes the
  machine.
- The display window takes no mouse input, does not go full screen with the
  interface, and moves on its own; closing it does nothing.
- Two path helpers (`PathExtensions.IsSubfolderOf`, `GetRelativePath`) call
  GNU realpath (`-Lm`, `--relative-to`), which macOS's lacks; their tests fail
  here.
- The interface tests run under XQuartz until XQuartz itself stops ("Could
  not destroy window"), about 95 tests in, at a test that re-opens a context
  menu.

## Mesa: Vulkan and the newer OpenGL

Apple's OpenGL stops at 4.1 and macOS has no Vulkan of its own. Mesa has
both for Apple silicon (user-decided, 2026-10-20):

- **KosmicKrisp**, Mesa's Vulkan driver on Metal (macOS 26 or later). It is
  conformant Vulkan 1.4, unlike MoltenVK, which is a subset. The Vulkan
  bridge draws on it: when the same GPU is listed by both, the bridge takes
  the driver that is not a subset (`VK_KHR_portability_subset`).
- **zink**, Mesa's OpenGL on Vulkan, on KosmicKrisp. The OpenGL bridge loads
  Mesa's EGL (`libEGL.1.dylib`) when a core asks for the bridge and makes a
  surfaceless context, as on Linux; if there is no Mesa, or it makes no
  context, it uses Apple's OpenGL as before, and says why on stderr
  (`chimera gl: ...; using Apple's OpenGL`). `CHIMERA_GL_APPLE=1` keeps to
  Apple's.

KosmicKrisp has no geometry shaders and no transform feedback (Mesa 26.2.5,
and Mesa's main branch on 2026-10-20), so zink calls itself OpenGL 2.1 on it.
Everything else of OpenGL 4.6 it has, so the bridge tells Mesa to say 4.6
(`MESA_GL_VERSION_OVERRIDE=4.6`, `MESA_GLSL_VERSION_OVERRIDE=460`, unless
they are already set). A renderer that uses one of the two missing stages
finds out when it compiles the shader.

Mesa is built from its release with two small patches, kept beside the build
script (`build-mesa.sh` and `patches/` in the macOS working folder, outside
this repository):

- EGL's surfaceless platform loads zink without a DRM device (macOS has
  none) without asking it for a software Vulkan device;
- zink, like the Vulkan bridge, prefers a device that is not a portability
  subset when MoltenVK and KosmicKrisp both list the GPU.

Mesa's libraries go on `DYLD_FALLBACK_LIBRARY_PATH` (the build has no
`libGL.dylib`, so nothing stands in for OpenGL.framework), and its
KosmicKrisp manifest on `VK_ADD_DRIVER_FILES`:

```sh
export DYLD_FALLBACK_LIBRARY_PATH="<mesa>/lib:/opt/homebrew/lib:/usr/local/lib:/usr/lib"
export VK_ADD_DRIVER_FILES="<mesa>/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json"
```

Measured on an M1 (2026-10-20, macOS 27, Mesa 26.2.5, the PCSX2 core's arm64
package, `chimera-run --gpu`, 1500 frames of a Klonoa 2 movie):

- `vulkan-hw`: `chimera vk: Apple M1 (Vulkan 1.4.354, driver 26.2.5)`, with
  MoltenVK installed too. 58 s; 11.9 s of it is KosmicKrisp compiling 173
  pipelines.
- `opengl-hw`: `chimera gl: 4.6 (Core Profile) Mesa 26.2.5 on zink Vulkan
  1.4(Apple M1 (MESA_KOSMICKRISP))`. 61 s.
- `software`: 64 s.
- The two hardware renderers' pictures at frames 1000 and 1499 are the same
  colour in every pixel. The software renderer's is the same at 1000; at
  1499, 17% of pixels differ, nearly all by 8 in 255 or less.
- The engine's tests pass with Mesa and with `CHIMERA_GL_APPLE=1`, and
  miniBox's Vulkan bridge tests pass on KosmicKrisp.

Not tried yet: the interface with a hardware renderer on Mesa (only
`chimera-run` has run it), and a renderer that needs geometry shaders.
