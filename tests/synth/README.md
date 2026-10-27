# The synthetic witness

Chimera's own, core-agnostic correctness apparatus (see "The synthetic
witness" in [design-principles.md](../../docs/design-principles.md)). Two
implementations of the same tiny console - a native reference and a
waterboxed core - must give, for every test movie, the same memory, video
output and audio output, bit for bit.

Everything here is a conformance-test fixture for the published core contract:
nothing is quickerNES- or system-specific.

## Running

```
./run-witness.sh              # both levels, both modes
./run-witness.sh --level a    # native only (no Chimera needed, <1 s)
./run-witness.sh --record     # (re)record goldens
```

## Layout

- `SPEC.md` - the Synth machine specification (v1). The single source of
  truth; both implementations are written against it alone.
- `native/synthcore.c` - the native reference implementation, and
  `native/synth-run.c` its Level A runner.
- `package-box/` - the waterboxed core (`synth_wbx.c`), its runner and its
  package's declarations; `build-box.sh` builds it and `../build-package.sh`
  installs `build/Cores/synth-box.chimeraCore`.
- `asm.py` - assembler: `.sasm` text -> `.testrom`.
- `roms/` - the test games (game logic, video assets, and audio assets live
  in the roms; the emulator is a bare stateful interpreter).
- `goldens/`, `movies/` - the recorded results and the movies that make them.
- `*.lua`, `*.py` - the scripts and checks the witness's levels run.

## Status

- [x] Machine spec v1
- [x] Native core + assembler + gridWalker rom
- [x] Waterboxed core, verified against the native goldens: bit-identical
      RAM, video stream and audio stream on every movie
- [x] Witness driver (`run-witness.sh`): four goal movies (win / lose /
      video output / audio output), Level A native metrics incl. full
      per-frame video+audio stream hashes with a rerecord self-check, Level B
      through Chimera's window, run `--headless` with no display, with
      RAM+VRAM byte-compare against the same goldens - THE Chimera smoke test.
- [ ] Additional test roms (dedicated video-goal and audio-goal roms)

## Provenance

All code, art, and sound here are original to this repository (the game logic
derives from the author's own JaffarPlus TestEmulator/GridWalker, MIT): the
tiles are elementary geometry, the palette is generic RGB values, and the
audio is square-wave frequency ramps - nothing is taken from any existing
game, emulator, or other copyrighted source.
