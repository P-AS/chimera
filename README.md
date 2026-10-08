<p align="center">
	<a href="https://github.com/ToolAssisted-run/chimera/actions/workflows/ci.yml"><img src="https://github.com/ToolAssisted-run/chimera/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
	<a href="https://github.com/ToolAssisted-run/chimera/releases/tag/dev"><img src="https://img.shields.io/github/v/release/ToolAssisted-run/chimera?include_prereleases&sort=date&label=download&color=2DB3A6" alt="Latest development build"></a>
	<a href="https://github.com/ToolAssisted-run/chimera/releases"><img src="https://img.shields.io/github/downloads/ToolAssisted-run/chimera/total?label=downloads&color=8A63E8" alt="Downloads"></a>
	<a href="https://discord.gg/VsKDT9XB6u"><img src="https://img.shields.io/discord/1537060793894314097?logo=discord&logoColor=white&label=discord&color=5865F2" alt="toolAssisted.run on Discord"></a>
</p>

<p align="center">
	<picture>
		<source media="(prefers-color-scheme: dark)" srcset="docs/icon-dark.svg">
		<img src="docs/icon.svg" alt="Chimera - four pixel modules around the beast's eye" width="140" align="middle">
	</picture>
	&nbsp;&nbsp;
	<picture>
		<source media="(prefers-color-scheme: dark)" srcset="docs/logotype-dark.svg">
		<img src="docs/logotype.svg" alt="CHIMERA" width="330" align="middle">
	</picture>
</p>

Chimera is a minimal frontend for creating tool-assisted speedruns (TAS).

## Goals

- **Modularity.** The frontend contains no emulation core and no system-specific knowledge. Cores are external, self-contained packages (`.chimeraCore`), each maintained in its own repository under its own license, loaded explicitly like a ROM. Chimera includes none and downloads none.

- **Performance.** All functional machinery (the sandbox host, movies, savestates, file formats, the running machine itself) lives in `libchimera`, a native C++ engine the GUI calls into.

- **Stronger reproducibility guarantees.** Every core runs inside the [miniBox](https://github.com/ToolAssisted-run/chimera-common-minibox) sandbox, so the same project and input files play the same movie on any machine.

Chimera is not designed for casual play. For that, use the original emulators directly, or a multi-emulation frontend such as RetroArch.

## Cores

Chimera does not include cores and does not download them. To use a core, download its `.chimeraCore` package from the core's own project (linked below) or build it yourself, and put it in the `Cores` folder beside Chimera.

**File > Core Manager** lists the cores in that folder and lets you point Chimera at a different one ([docs/core-manager.md](docs/core-manager.md)).

### Emulation cores

| System | Core |
| --- | --- |
| Nintendo Entertainment System / Famicom | [quickerNES](https://github.com/ToolAssisted-run/chimera-core-quickernes), [QuickerNesHawk](https://github.com/ToolAssisted-run/chimera-core-neshawk), [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Famicom Disk System | [QuickerNesHawk](https://github.com/ToolAssisted-run/chimera-core-neshawk) |
| Super Nintendo | [Snes9x](https://github.com/ToolAssisted-run/chimera-core-snes9x), [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Satellaview | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Nintendo 64 | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| GameCube | [Dolphin](https://github.com/ToolAssisted-run/chimera-core-dolphin) |
| Wii | [Dolphin](https://github.com/ToolAssisted-run/chimera-core-dolphin) |
| Game Boy / Game Boy Color | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Game Boy Advance | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Nintendo 3DS / New Nintendo 3DS | [Azahar](https://github.com/ToolAssisted-run/chimera-core-azahar) |
| Mega Drive / Genesis | [Genesis Plus GX](https://github.com/ToolAssisted-run/chimera-core-gpgx), [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Mega Drive 32X | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Sega CD / Mega CD | [Genesis Plus GX](https://github.com/ToolAssisted-run/chimera-core-gpgx), [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Sega CD 32X | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Master System | [Genesis Plus GX](https://github.com/ToolAssisted-run/chimera-core-gpgx), [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Game Gear | [Genesis Plus GX](https://github.com/ToolAssisted-run/chimera-core-gpgx), [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| SG-1000 | [Genesis Plus GX](https://github.com/ToolAssisted-run/chimera-core-gpgx), [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Dreamcast | [Flycast](https://github.com/ToolAssisted-run/chimera-core-flycast) |
| Sega NAOMI / NAOMI 2 (arcade) | [Flycast](https://github.com/ToolAssisted-run/chimera-core-flycast) |
| Sammy Atomiswave (arcade) | [Flycast](https://github.com/ToolAssisted-run/chimera-core-flycast) |
| Capcom CPS-1 / CPS-2 / CPS-3 (arcade) | [FBNeo](https://github.com/ToolAssisted-run/chimera-core-fbneo) |
| Neo Geo MVS (arcade) | [FBNeo](https://github.com/ToolAssisted-run/chimera-core-fbneo) |
| Neo Geo CD | [FBNeo](https://github.com/ToolAssisted-run/chimera-core-fbneo) |
| Sega System 16 (arcade) | [FBNeo](https://github.com/ToolAssisted-run/chimera-core-fbneo) |
| PlayStation | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| PlayStation 2 | [PCSX2](https://github.com/ToolAssisted-run/chimera-core-pcsx2) |
| PlayStation Portable | [PPSSPP](https://github.com/ToolAssisted-run/chimera-core-ppsspp) |
| PlayStation Vita | [Vita3K](https://github.com/ToolAssisted-run/chimera-core-vita3k) |
| PlayStation 3 | [RPCS3](https://github.com/ToolAssisted-run/chimera-core-rpcs3) |
| Xbox | [xemu](https://github.com/ToolAssisted-run/chimera-core-xemu) |
| 3DO Interactive Multiplayer | [Opera](https://github.com/ToolAssisted-run/chimera-core-opera) |
| Atari 2600 | [Stella](https://github.com/ToolAssisted-run/chimera-core-stella), [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Atari 5200 | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| ColecoVision | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| MSX / MSX2 | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| PC Engine / TurboGrafx-16 / SuperGrafx | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| PC Engine CD / TurboDuo | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Neo Geo AES | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Neo Geo Pocket / Color | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| WonderSwan / WonderSwan Color | [ares](https://github.com/ToolAssisted-run/chimera-core-ares) |
| Apple II / II Plus / IIe (and the Pravets, TK3000 and Base64A clones) | [AppleWin](https://github.com/ToolAssisted-run/chimera-core-applewin) |
| Sharp X68000 | [MAME X68000](https://github.com/ToolAssisted-run/chimera-core-x68k) |
| MS-DOS | [DOSBox-X](https://github.com/ToolAssisted-run/chimera-core-dosbox-x), [PCem](https://github.com/ToolAssisted-run/chimera-core-pcem) |
| Windows 3.1 / 95 / 98 | [DOSBox-X](https://github.com/ToolAssisted-run/chimera-core-dosbox-x), [PCem](https://github.com/ToolAssisted-run/chimera-core-pcem) |
| Windows XP | [PCem](https://github.com/ToolAssisted-run/chimera-core-pcem) |
| Linux (x86) | [PCem](https://github.com/ToolAssisted-run/chimera-core-pcem) |
| Flash | [Ruffle](https://github.com/ToolAssisted-run/chimera-core-ruffle) |
| Symbian / Nokia N-Gage | [EKA2L1](https://github.com/ToolAssisted-run/chimera-core-eka2l1) |
| iPhone OS 2.x-4.0 (iPhone, iPod touch and iPad apps) | [touchHLE](https://github.com/ToolAssisted-run/chimera-core-touchhle) |

### Game cores

One game each, run from the game's own files ([docs/game-cores.md](docs/game-cores.md)).

| Game | Core |
| --- | --- |
| Prince of Persia (DOS) | [SDLPoP](https://github.com/ToolAssisted-run/chimera-core-sdlpop) |
| Prince of Persia 2: The Shadow and the Flame (DOS) | [SDLPoP2](https://github.com/ToolAssisted-run/chimera-core-sdlpop2) |
| Sword of the Samurai (DOS) | [OpenSamurai](https://github.com/ToolAssisted-run/chimera-core-opensamurai) |
| Syndicate (DOS) | [SyndicatFX](https://github.com/ToolAssisted-run/chimera-core-syndicatfx) |
| Another World | [rawgl](https://github.com/ToolAssisted-run/chimera-core-rawgl) |
| Doom, Doom II, Final Doom, Heretic, Hexen, Chex Quest, Freedoom | [DSDA-Doom](https://github.com/ToolAssisted-run/chimera-core-dsda) |

### Which emulator each core builds

Each core's repository pins its emulator at one commit. This table is that commit and its date, written by `tools/core-versions.py` from the cores' own repositories on 2026-10-08; a core's repository is what is true today.

<!-- core-versions:begin -->
| Core | Emulator source | Commit | Its date | Nearest release |
| --- | --- | --- | --- | --- |
| [quickerNES](https://github.com/ToolAssisted-run/chimera-core-quickernes) | [SergioMartin86/QuickNES_Core](https://github.com/SergioMartin86/QuickNES_Core) | [`5a146c579`](https://github.com/SergioMartin86/QuickNES_Core/commit/5a146c5795a0b68c8a05141c71d5984243d047f7) | 2025-09-17 |  |
| [QuickerNesHawk](https://github.com/ToolAssisted-run/chimera-core-neshawk) | in the core's own repository | | | |
| [Genesis Plus GX](https://github.com/ToolAssisted-run/chimera-core-gpgx) | [ekeeke/Genesis-Plus-GX](https://github.com/ekeeke/Genesis-Plus-GX) | [`27426f00a`](https://github.com/ekeeke/Genesis-Plus-GX/commit/27426f00aa68f9f358c86919e8a40985326fa05b) | 2026-08-04 |  |
| [Snes9x](https://github.com/ToolAssisted-run/chimera-core-snes9x) | [snes9xgit/snes9x](https://github.com/snes9xgit/snes9x) | [`2971061cf`](https://github.com/snes9xgit/snes9x/commit/2971061cf07fdc6fc7d18883edf4e648eb16a6d2) | 2026-08-17 |  |
| [Stella](https://github.com/ToolAssisted-run/chimera-core-stella) | [stella-emu/stella](https://github.com/stella-emu/stella) | [`c1ffb833c`](https://github.com/stella-emu/stella/commit/c1ffb833c8b180433b0cad76bb6b55f8dfbc46ee) | 2026-08-23 |  |
| [Opera](https://github.com/ToolAssisted-run/chimera-core-opera) | [libretro/opera-libretro](https://github.com/libretro/opera-libretro) | [`a501a278d`](https://github.com/libretro/opera-libretro/commit/a501a278d057b952d1ad6165549c59ab178ca497) | 2026-08-21 |  |
| [DOSBox-X](https://github.com/ToolAssisted-run/chimera-core-dosbox-x) | [joncampbell123/dosbox-x](https://github.com/joncampbell123/dosbox-x) | [`784240ad6`](https://github.com/joncampbell123/dosbox-x/commit/784240ad6d9cf3ae3f02fab819e2ed5cf5117dd4) | 2026-08-02 |  |
| [Flycast](https://github.com/ToolAssisted-run/chimera-core-flycast) | [flyinghead/flycast](https://github.com/flyinghead/flycast) | [`c3763d8fc`](https://github.com/flyinghead/flycast/commit/c3763d8fc4208dd6f8f0bc456383543b8406a8a0) | 2026-08-23 |  |
| [Dolphin](https://github.com/ToolAssisted-run/chimera-core-dolphin) | [dolphin-emu/dolphin](https://github.com/dolphin-emu/dolphin) | [`a1e636d72`](https://github.com/dolphin-emu/dolphin/commit/a1e636d72c8469acf747ac6542f0b7ace7cea02f) | 2026-09-01 |  |
| [PPSSPP](https://github.com/ToolAssisted-run/chimera-core-ppsspp) | [hrydgard/ppsspp](https://github.com/hrydgard/ppsspp) | [`fa50bb197`](https://github.com/hrydgard/ppsspp/commit/fa50bb1976065c4f8b1b47af227d367fe9771555) | 2026-05-16 |  |
| [PCSX2](https://github.com/ToolAssisted-run/chimera-core-pcsx2) | [PCSX2/pcsx2](https://github.com/PCSX2/pcsx2) | [`e1dd0a085`](https://github.com/PCSX2/pcsx2/commit/e1dd0a08599e86a9928a83b84923bce12a59aba7) | 2026-08-24 |  |
| [RPCS3](https://github.com/ToolAssisted-run/chimera-core-rpcs3) | [RPCS3/rpcs3](https://github.com/RPCS3/rpcs3) | [`677e13da4`](https://github.com/RPCS3/rpcs3/commit/677e13da42a36dc16eef090cc2a7d4d22e48aa5a) | 2026-09-01 | v0.0.42 + 234 commits |
| [xemu](https://github.com/ToolAssisted-run/chimera-core-xemu) | [xemu-project/xemu](https://github.com/xemu-project/xemu) | [`d73326b62`](https://github.com/xemu-project/xemu/commit/d73326b62199c6dd952ef512947710e1333a49d3) | 2026-08-26 | v0.8.136 + 30 commits |
| [EKA2L1](https://github.com/ToolAssisted-run/chimera-core-eka2l1) | [EKA2L1/EKA2L1](https://github.com/EKA2L1/EKA2L1) | [`1bc5c8cf2`](https://github.com/EKA2L1/EKA2L1/commit/1bc5c8cf2e8c1dcf04475bcc5dc3e49b7282d47a) | 2026-09-05 |  |
| [ares](https://github.com/ToolAssisted-run/chimera-core-ares) | [ares-emulator/ares](https://github.com/ares-emulator/ares) | [`af4cbb04f`](https://github.com/ares-emulator/ares/commit/af4cbb04f067682a8a3cf42695ff78bed634b38d) | 2026-09-06 |  |
| [Ruffle](https://github.com/ToolAssisted-run/chimera-core-ruffle) | [ruffle-rs/ruffle](https://github.com/ruffle-rs/ruffle) | [`a1dd7bbc1`](https://github.com/ruffle-rs/ruffle/commit/a1dd7bbc180b6ee1cc2fcadf69e89c0e1c7477e6) | 2026-09-06 |  |
| [AppleWin](https://github.com/ToolAssisted-run/chimera-core-applewin) | [AppleWin/AppleWin](https://github.com/AppleWin/AppleWin) | [`3e8054b46`](https://github.com/AppleWin/AppleWin/commit/3e8054b4627624398e4589f7f27b3d40a6b9718e) | 2026-07-26 | v1.32.0.0 + 12 commits |
| [PCem](https://github.com/ToolAssisted-run/chimera-core-pcem) | [TASEmulators/pcem](https://github.com/TASEmulators/pcem) | [`fd4585bb1`](https://github.com/TASEmulators/pcem/commit/fd4585bb1eb2c411819391c7241c1ed5b621dfc7) | 2026-07-25 |  |
| [FBNeo](https://github.com/ToolAssisted-run/chimera-core-fbneo) | [finalburnneo/FBNeo](https://github.com/finalburnneo/FBNeo) | [`6bde5e1dd`](https://github.com/finalburnneo/FBNeo/commit/6bde5e1dddb7dd53c1964e1a2c3d33f276870469) | 2026-09-26 |  |
| [Azahar](https://github.com/ToolAssisted-run/chimera-core-azahar) | [azahar-emu/azahar](https://github.com/azahar-emu/azahar) | [`955ef51a2`](https://github.com/azahar-emu/azahar/commit/955ef51a27f2e2c3de340ec0f972407aef955eca) | 2026-09-26 |  |
| [MAME X68000](https://github.com/ToolAssisted-run/chimera-core-x68k) | [mamedev/mame](https://github.com/mamedev/mame) | [`0cacb1a76`](https://github.com/mamedev/mame/commit/0cacb1a76d20d1b80a9e84185dabd758ee2ec876) | 2026-09-28 |  |
| [touchHLE](https://github.com/ToolAssisted-run/chimera-core-touchhle) | [touchHLE/touchHLE](https://github.com/touchHLE/touchHLE) | [`8eb3418b3`](https://github.com/touchHLE/touchHLE/commit/8eb3418b3343bc1e3d1a330b55cb1d708a015721) | 2026-10-01 | v0.3.0 |
| [Vita3K](https://github.com/ToolAssisted-run/chimera-core-vita3k) | [Vita3K/Vita3K](https://github.com/Vita3K/Vita3K) | [`a366df69b`](https://github.com/Vita3K/Vita3K/commit/a366df69bd66bedaa245e40902d1992038f7ccb7) | 2026-10-02 |  |
| [SDLPoP](https://github.com/ToolAssisted-run/chimera-core-sdlpop) | [NagyD/SDLPoP](https://github.com/NagyD/SDLPoP) | [`3c5add5fb`](https://github.com/NagyD/SDLPoP/commit/3c5add5fb7f83d4ceb542823ab66d00146c4271b) | 2025-12-24 | v1.24-RC + 17 commits |
| [SDLPoP2](https://github.com/ToolAssisted-run/chimera-core-sdlpop2) | [ToolAssisted-run/SDLPoP2](https://github.com/ToolAssisted-run/SDLPoP2) | [`37112dadd`](https://github.com/ToolAssisted-run/SDLPoP2/commit/37112dadd7301f2b8462415eec068b52fff5a8e4) | 2026-10-04 |  |
| [OpenSamurai](https://github.com/ToolAssisted-run/chimera-core-opensamurai) | [ToolAssisted-run/OpenSamurai](https://github.com/ToolAssisted-run/OpenSamurai) | [`b40625a3e`](https://github.com/ToolAssisted-run/OpenSamurai/commit/b40625a3e039206d030d7a7f3bff2745069d4193) | 2026-10-03 |  |
| [SyndicatFX](https://github.com/ToolAssisted-run/chimera-core-syndicatfx) | [swfans/syndicatfx](https://github.com/swfans/syndicatfx) | [`36320614c`](https://github.com/swfans/syndicatfx/commit/36320614c4c76363a9d225ab5022123ca70cfd83) | 2025-12-17 | 0.0.6.1022 + 13 commits |
| [rawgl](https://github.com/ToolAssisted-run/chimera-core-rawgl) | [cyxx/rawgl](https://github.com/cyxx/rawgl) | [`049e4ade4`](https://github.com/cyxx/rawgl/commit/049e4ade49543a12414f68a7838a94ec0a6c149d) | 2025-06-28 | rawgl-0.2.1 + 221 commits |
| [DSDA-Doom](https://github.com/ToolAssisted-run/chimera-core-dsda) | [kraflab/dsda-doom](https://github.com/kraflab/dsda-doom) | [`8c538098f`](https://github.com/kraflab/dsda-doom/commit/8c538098f193c66bbec96e4cf3c96a51f2754ec1) | 2026-09-29 | v0.30.0 |
<!-- core-versions:end -->

## Getting a build

The frontend is built for Linux and Windows and published here:

- [**Latest development build**](https://github.com/ToolAssisted-run/chimera/releases/tag/dev) - rebuilt on every change to `main` that passes the gates, and replaced each time. Nothing is published that did not pass them. **Not for submissions:** a dev build is replaced on every change, so it may stop being downloadable and a movie made on it can stop being replayable. Do not use one to produce a TAS for submission to toolAssisted.run - use a nightly.
- [**Nightly builds**](https://github.com/ToolAssisted-run/chimera/releases) - dated, immutable, and kept forever. Cite one of these in a bug report or beside a movie: a run is only reproducible while the build that recorded it still exists, and this is what a TAS submitted to toolAssisted.run should be made on.

A bundle carries no cores, and Chimera never reaches the network - it downloads
nothing, not a core and not a list of them. To set one up:

1. Download a build above and unpack it.
2. Download the `.chimeraCore` package of each core you want from that core's
   releases page (the [Cores](#cores) tables link them), or build it.
3. Put the packages in the `Cores` folder beside `Chimera.exe`, and start Chimera.

Each core publishes a `dev` build and nightly releases the same way, and its
nightlies are never deleted - which is what lets a movie name the exact package
that recorded it and still be replayable years later.

Every bundle carries `BUILD.txt`, naming the exact commit it was built from, and
`LICENSES.md`, stating its terms. **Adding a core adds that core's terms**,
and some of them (Genesis Plus GX, Opera, Snes9x) forbid commercial use, which
binds whatever they are installed into; Chimera shows a core's licence once it
is in the folder.

Core packages published before the split are kept in the
[`cores`](https://github.com/ToolAssisted-run/chimera/releases/tag/cores)
release, named by SHA1. It no longer grows - each core archives its own now -
but movies recorded then still cite packages in it.

## Building

The canonical build is Linux-hosted and meson-mediated, and produces the artifacts for both operating systems: the managed frontend is built once (platform-neutral IL, .NET Framework on Windows / Mono on Linux), and every native library is built twice: gcc for Linux, mingw-w64 cross for Windows. Clone with `--recursive`; the repository contains no precompiled binaries, and
no cores - `tools/fetch-cores.sh` puts the published ones in `build/Cores` if
you want a working set without opening the frontend.

```
meson setup build/meson-linux   --prefix "$(pwd)/build" --libdir dll
meson setup build/meson-windows --prefix "$(pwd)/build" --libdir dll --cross-file extern/meson/mingw-w64.ini
meson compile -C build/meson-linux && meson install -C build/meson-linux
meson compile -C build/meson-windows && meson install -C build/meson-windows
meson compile -C build/meson-linux frontend   # the managed solution (dotnet)
```

Linux requirements: meson, ninja, cmake, gcc, mingw-w64, and Microsoft's own .NET SDK binary (`curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0`); distro-built SDKs omit the WindowsDesktop targets the net48/WinForms frontend needs.

The frontend ships no cores, so get at least one before running it - either the
published packages, or a core repository cloned wherever you like:

```
tools/fetch-cores.sh                                # every published core -> build/Cores
<core checkout>/waterbox/build-package.sh -r $PWD   # or build one yourself
tools/build-bundle.sh --platform linux --out <dir>  # the distributable (no cores)
```

To run: `build\Chimera.exe` on Windows, `build/ChimeraMono.sh` on Linux, then
`File > New Project...` and pick a core. To play a rom with no project, pass
`--core=<package> <rom>` on the command line.

The witness gate runs with `tests/synth/run-witness.sh`. The engineering log (objectives, procedure, and the sharp edges found along the way) is in [docs/design-principles.md](docs/design-principles.md); the engine migration is chronicled in [docs/engine-migration.md](docs/engine-migration.md). Building a new core, and joining it to this bundle, is [docs/porting-a-core.md](docs/porting-a-core.md).

## Reporting a problem

Open an issue on this repository, whichever core it concerns - one inbox,
and the issue template asks for what a fix needs. What settles most
reports before anybody opens a debugger:

- **The build strings.** Help > Copy Version Info puts the frontend's build
  and the running core's version on the clipboard, in the template's words.
  A frontend and a core from different days may not
  understand each other's states, so before reporting a save/load problem,
  match them.
- **The project file.** A `.chimeraProject` is small and names every file
  by hash, so attach it rather than describing it.
- **The three crash files.** When Chimera or a core dies it writes a crash
  note and a minidump, `<date> pid<N>.txt` and `.dmp`, into the `Crashes`
  folder of the data directory (Config > Data Directory... opens it;
  `%LOCALAPPDATA%\Chimera` by default on Windows), and the sandbox writes
  `minibox-diag.log` next to `Chimera.exe`. Attach all three: the note
  carries the faulting instruction and the machine's last words, and two of
  three crashes in one recent report were fixed off those files alone.
- **The core's log.** Tools > Export Core Log... keeps everything the core
  says in a file you choose; it is off until asked for, and reboots the core
  so the log starts at boot. Use it again to turn it off, and attach the file.

## Contributing

Pull requests are welcome, from people and from people working with AI assistants alike. A contribution is judged on its merits: it should build, pass the witness gate, and keep to the project's scope. The one firm requirement is legal cleanliness: you must have the right to submit the code under this repository's MIT license, and anything derived from other works must respect their licenses and carry the attribution they require.

## Credits and license

**Chimera is a derivative fork of [BizHawk](https://github.com/TASEmulators/BizHawk).** most of the frontend, TAS tooling, and the architecture it builds on are the original work of the BizHawk team, and all credit for them belongs to BizHawk's developers.

Chimera is provided under the MIT License, preserving the BizHawk team's copyright; see [LICENSE](LICENSE) for the terms and [NOTICE](NOTICE) for whose work it is and what the terms do not cover: the native libraries built from `extern/`, the vendored test suite, and why core packages carry their own licenses. The people behind Chimera itself are in [CREDITS.md](CREDITS.md).
