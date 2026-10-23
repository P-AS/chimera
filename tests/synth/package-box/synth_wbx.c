/* synth.wbx - the waterboxed flavor (c) of the Synth machine.
 *
 * The machine itself is the SAME reference implementation as flavors (a)/(b):
 * native/synthcore.c, compiled unchanged for the guest. This file is only the
 * thin waterbox ABI layer over it. The whole machine state lives in guest
 * memory, so the miniBox host savestates it automatically - there is no
 * explicit serialize/deserialize here, unlike the native and C# adapters.
 * That is the point of the waterbox flavor: reproducibility by construction.
 *
 * The rom arrives as a mounted file "rom" (read at Init, per the side-effect
 * rule - all data through the host interface, never a host path).
 */
#include <emulibc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sched.h>

#include <waterbox_settings.h>  /* miniBox guest kit: read the host settings channel */

/* synthcore.c public API (see tests/synth/SPEC.md). */
typedef struct synth synth_t;
extern synth_t *synth_create(const uint8_t *rom, uint32_t romSize);
extern void synth_reset(synth_t *s);
extern void synth_frame(synth_t *s, uint8_t pad);
extern uint8_t *synth_get_ram(synth_t *s);
extern const uint8_t *synth_get_framebuffer(synth_t *s);
extern const int16_t *synth_get_audio(synth_t *s);
extern uint8_t synth_input_was_read(synth_t *s);
extern void synth_get_video_bgra(synth_t *s, uint32_t *out);  /* palette-resolved 128x120 BGRA */
extern int32_t *synth_get_registers(synth_t *s);
extern uint32_t synth_get_frame_counter(synth_t *s);
extern void (*synth_on_step)(uint32_t pc, const uint8_t *insn, const int32_t *r);

#define FB_W 128
#define FB_H 120

static synth_t *g_synth;
static uint32_t g_video[FB_W * FB_H];

/* Reads the whole mounted "rom" file into a buffer (caller frees). */
static uint8_t *read_rom(uint32_t *out_len) {
	FILE *f = fopen("rom", "rb");
	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
	if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return 0; }
	fclose(f);
	*out_len = (uint32_t)n;
	return buf;
}

/* A page no tool may disturb (leg E:tools:core-not-run). The core makes it
 * no-access the way RPCS3's texture cache watches memory, and its fault
 * handler counts every access that reaches it, opens the page, and the next
 * frame closes it again. The engine's tools read a domain through miniBox's
 * read view, which never reaches the handler: before that, a tool's read
 * between frames ran the handler on the tool's thread, and a handler that
 * writes memory (RPCS3's writes the picture back) changed the machine
 * (engine analysis, 2026-10-18). The page holds byte i = i * 7 + 3; the count
 * is in the Mailbox. Both are in every state, as RPCS3's memory is. */
static uint8_t *g_watchedPage;
static uint32_t g_handlerRuns;
static int g_watchedOpen;

ECL_EXPORT int GuestFaultHandler(uint64_t addr, uint64_t is_write)
{
	(void)is_write;
	if (g_watchedPage == 0 || addr < (uintptr_t)g_watchedPage || addr >= (uintptr_t)g_watchedPage + 4096) return 0;
	g_handlerRuns++;
	g_watchedOpen = 1;
	return mprotect(g_watchedPage, 4096, PROT_READ) == 0;
}

static volatile uint64_t g_spun;
static uint32_t g_spinFrame;   /* SPINFRAME: the frame that never ends, or 0 */

/* The compile cache's bridge, handed over when the frontend keeps a cache
 * (SetCacheBridge). A rom ending in CACHEWORK spends its third frame only
 * asking it for an object it never stored, and yielding: work the frontend
 * does for the core, which its watchdog must count as the core working, not
 * stuck (2026-10-19). */
typedef ECL_ENTRY uint64_t (*cache_bridge_fn)(uint64_t op, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e);
static cache_bridge_fn g_cache;
static uint32_t g_cacheWorkFrame;

/* A rom ending in SLOWFIRST makes the first frame after starting, and the
 * first after every state load, long: half a second or so in the core's own
 * code, with no system call, as RPCS3's first frame is while it analyses and
 * compiles a game. The watchdog gives those frames the start's time, not a
 * frame's (2026-10-21, issue 332). Invisible: the flag is in no state. */
static int g_slowFirst;
ECL_INVISIBLE static int g_slowNext;
static void slow_first(void) {
	if (!g_slowNext) return;
	g_slowNext = 0;
	fprintf(stderr, "synth: a long first frame\n");
	for (uint32_t i = 0; i < (1u << 31); i++) g_spun++;
}
/* A rom ending in STOPONCE: the frame the session stops partway, and what it
 * left behind (chimera#330). In frame 5 the core scribbles over the
 * machine's RAM and leaves a block of its heap allocated and written, then
 * loops in its own code until the session's watchdog stops it - the first
 * time only (invisible: no state remembers it). Every frame also allocates
 * and frees a block whose size changes, so the heap's own bookkeeping is in
 * every state. A machine put back on an earlier stored frame must carry none
 * of the dead frame's writes: played on, it ends on the goldens. */
static uint32_t g_framesRun;           /* defined with StateSaving, below */
static int g_stopOnce;
ECL_INVISIBLE static int g_stoppedOnce;
static void *g_leftBehind;
static void heap_churn(void) {
	if (!g_stopOnce) return;
	size_t n = 100 + (g_framesRun % 50) * 64;
	uint8_t *q = malloc(n);
	if (q) { memset(q, (int)g_framesRun, n); free(q); }
}
static void stop_once(void) {
	if (!g_stopOnce || g_stoppedOnce || g_framesRun != 5) return;
	g_stoppedOnce = 1;
	memset(synth_get_ram(g_synth), 0xA5, 4096);
	g_leftBehind = malloc(65536);
	if (g_leftBehind) memset(g_leftBehind, 0x5A, 65536);
	fprintf(stderr, "synth: frame 5 scribbled and stuck on purpose\n");
	for (;;) g_spun++;
}
ECL_EXPORT void SetCacheBridge(uint64_t fn) { g_cache = (cache_bridge_fn)(uintptr_t)fn; }
static void cache_work(void) {
	static const char name[] = "synth/never-stored";
	struct { uint64_t name, name_len, dst, cap; } fetch = { (uintptr_t)name, sizeof name - 1, 0, 0 };
	for (uint32_t i = 0; i < 1000000; i++) {
		if (g_cache) g_cache(1 /* CACHE_OP_FETCH */, (uintptr_t)&fetch, 0, 0, 0, 0);
		sched_yield();
	}
}

static int moving(void); /* the movingProperties setting, read once here */
/* The trace group (a trace logger): a line for each instruction run while
 * tracing is on - where, its bytes, then the registers after a tab - kept in
 * a buffer the host drains each frame. Invisible: the buffer and the flag are
 * in no state, and the engine says again after a load whether to trace. */
#define TRACE_BYTES (1 << 20)
ECL_INVISIBLE static char g_trace[TRACE_BYTES];
ECL_INVISIBLE static int32_t g_traceUsed, g_traceLines, g_traceOverflow, g_traceOn;
static void traced(uint32_t pc, const uint8_t *insn, const int32_t *r)
{
	if (!g_traceOn) return;
	char line[192];
	const uint32_t imm = (uint32_t)insn[4] | ((uint32_t)insn[5] << 8) | ((uint32_t)insn[6] << 16) | ((uint32_t)insn[7] << 24);
	int n = snprintf(line, sizeof line, "%04X: %02X %d %d %d %08X\tR0:%08X R1:%08X R2:%08X R3:%08X R4:%08X R5:%08X R6:%08X R7:%08X",
		(unsigned)pc, insn[0], insn[1] & 7, insn[2] & 7, insn[3] & 7, (unsigned)imm, (unsigned)r[0], (unsigned)r[1], (unsigned)r[2],
		(unsigned)r[3], (unsigned)r[4], (unsigned)r[5], (unsigned)r[6], (unsigned)r[7]);
	if (n < 0 || n >= (int)sizeof line) return;
	if (g_traceUsed + n + 1 > TRACE_BYTES)
	{
		g_traceOverflow = 1;
		return;
	}
	memcpy(g_trace + g_traceUsed, line, (size_t)n + 1);
	g_traceUsed += n + 1;
	g_traceLines++;
}
ECL_EXPORT void TraceSetEnabled(int32_t on) { g_traceOn = on != 0; }
ECL_EXPORT int32_t TraceGetLineCount(void) { return g_traceLines; }
ECL_EXPORT const char *TraceGetBuffer(void) { return g_trace; }
ECL_EXPORT int32_t TraceGetOverflow(void) { return g_traceOverflow; }
ECL_EXPORT int32_t TraceGetUsedBytes(void) { return g_traceUsed; }
ECL_EXPORT void TraceClear(void) { g_traceUsed = g_traceLines = g_traceOverflow = 0; }
ECL_EXPORT const char *TraceGetHeader(void) { return "Where: opcode a b c immediate\tthe registers"; }

ECL_EXPORT int Init(void) {
	/* A game's own firmware (SuggestSettings answers it, with the game's own
	 * setting that calls for it): with that setting on, the game does not
	 * start unless the file is mounted under its id. */
	char logo[8];
	if (wbx_setting_str("gameLogo", logo, (int)sizeof logo) > 0 && strcmp(logo, "on") == 0) {
		FILE *l = fopen("gamelogo", "rb");
		if (!l) {
			fprintf(stderr, "synth: the game logo is not mounted\n");
			return 0;
		}
		fclose(l);
	}
	uint32_t len = 0;
	uint8_t *rom = read_rom(&len);
	if (!rom) return 0;
	/* A rom of the four bytes DIE! makes the core die in Init: the engine must
	 * say the core stopped, with its reason, not that it could not load the
	 * file (2026-10-18). */
	if (len == 4 && memcmp(rom, "DIE!", 4) == 0) {
		fprintf(stderr, "synth: dying in Init on purpose\n");
		abort();
	}
	/* A core that never comes back (2026-10-19): a rom of the four bytes
	 * SPIN loops forever in Init, and a rom ending in SPINFRAME loops forever
	 * in its third frame - in its own code, making no system call, so only the
	 * session's watchdog (wbx_stop_guest) can end it. */
	if (len == 4 && memcmp(rom, "SPIN", 4) == 0)
		for (;;) g_spun++;
	if (len >= 9 && memcmp(rom + len - 9, "SPINFRAME", 9) == 0) g_spinFrame = 3;
	if (len >= 9 && memcmp(rom + len - 9, "CACHEWORK", 9) == 0) g_cacheWorkFrame = 3;
	if (len >= 9 && memcmp(rom + len - 9, "SLOWFIRST", 9) == 0) g_slowFirst = g_slowNext = 1;
	if (len >= 8 && memcmp(rom + len - 8, "STOPONCE", 8) == 0) g_stopOnce = 1;
	g_synth = synth_create(rom, len);   /* copies the rom internally */
	/* the trace hook is always in place, so a state is the same traced or
	 * not; whether it writes anything is the invisible flag below */
	synth_on_step = traced;
	free(rom);
	if (!g_synth) return 0;
	synth_reset(g_synth);

	/* Demonstration setting: pre-fill RAM with a byte before the program runs.
	 * Default 0 leaves the machine identical to flavors a/b (goldens hold); a
	 * non-zero value proves the setting reached the guest (observable in RAM). */
	long fill = wbx_setting_long("initFillByte", 0);
	if (fill != 0) {
		uint8_t *ram = synth_get_ram(g_synth);
		for (int i = 0; i < 4096; i++) ram[i] = (uint8_t)fill;
	}
	moving();
	g_watchedPage = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (g_watchedPage == MAP_FAILED) return 0;
	for (int i = 0; i < 4096; i++) g_watchedPage[i] = (uint8_t)(i * 7 + 3);
	if (mprotect(g_watchedPage, 4096, PROT_NONE) != 0) return 0;
	/* one line on standard output, as real cores print: without a window the
	 * MCP server runs the core in its own process, and this line must not
	 * reach the agent's protocol stream */
	printf("synth: started\n");
	fflush(stdout);
	return 1;
}

/* The button mask is 64-bit (guest ABI): button i of waterbox.config is bit i.
 * The synth machine has 8 buttons, so only the low byte is meaningful here. */
/* All eight buttons at once is not a game input: it is how a test makes this
 * core die on cue - it says so, then abort()s the way a panicking core does;
 * all but Up follows a wild pointer instead -
 * so that everything above the sandbox can be tested against a core that stops
 * (miniBox hands control back; Chimera offers what to do next). No witness movie
 * presses all eight. */
/* --- a picture that is wrong for a while after a state load (chimera#190) ---
 * A renderer on the far side of the GPU bridge rebuilds what a load threw
 * away as the game touches it, and until it has, the picture it hands back is
 * wrong. The synth draws in software and never is; with its garbleAfterLoad
 * setting it pretends, for that many frames it draws after every load, so the witness
 * can hold the engine's kept pictures to something (leg E:kept-pictures). The
 * machine does not notice: none of this is in a state, and only the picture
 * handed out is touched - not the framebuffer the witness hashes. */
ECL_INVISIBLE static int g_render = 1;      /* SetRenderingEnabled: the core is drawing */
ECL_INVISIBLE static int g_garbleFor = -1; /* the setting, once asked */
ECL_INVISIBLE static int g_garbleLeft;     /* frames still to garble */
ECL_INVISIBLE static int g_garbleNow;      /* the frame just run is one of them */

ECL_EXPORT void StateLoaded(void)
{
	if (g_garbleFor < 0) g_garbleFor = (int)wbx_setting_long("garbleAfterLoad", 0);
	g_garbleLeft = g_garbleFor;
	g_slowNext = g_slowFirst;
}

/* --- told before a state is taken (StateSaving; chimera#190) ---
 * A core part of whose machine is somewhere a state does not reach - xemu's
 * surfaces, which live in the GPU driver until the processor reads them -
 * brings it into the machine when told a state is about to be taken. The
 * synth has no such part, so it keeps a note instead: the number of frames it
 * had run the last time it was told, in memory a state carries, as a renderer
 * would write a surface into the console's RAM. The witness (leg
 * E:state-saving) reads it back through the Mailbox with the two counts
 * beside it and asks what matters: a state that is restored holds the note of
 * ITS OWN frame - so the core was told before the state was read, and what it
 * wrote went into the delta - and a frame that was not stored was not told. */
static uint32_t g_framesRun;           /* in a state, like the machine */
static uint32_t g_toldAt;              /* in a state: frames run when last told */
ECL_INVISIBLE static uint32_t g_told;  /* times told this session: in no state */

/* Three more ways to die on cue, each armed by a frame's buttons and taken in
 * the next call of one export: all but Down in GetVideoBgra (the picture's
 * readback), all but Left in StateSaving, all but Right in InputWasRead. The
 * engine must notice a core that dies after its frame and not count, record or
 * store that frame (2026-10-18). Invisible: an armed cue is in no state. */
ECL_INVISIBLE static int g_dieIn;   /* 0 none, 1 GetVideoBgra, 2 StateSaving, 3 InputWasRead */
static void die_if_armed(int where, const char *name)
{
	if (g_dieIn != where) return;
	fprintf(stderr, "synth: dying in %s on purpose\n", name);
	abort();
}

ECL_EXPORT void StateSaving(void)
{
	die_if_armed(2, "StateSaving");
	g_told++;
	g_toldAt = g_framesRun;
}

ECL_INVISIBLE static uintptr_t (*g_inputPoll)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

ECL_EXPORT void FrameAdvance(uint64_t pad) {
	/* frames DRAWN, not frames run: a renderer rebuilds what a load threw away
	 * by drawing, so one told not to draw through a seek is as wrong on the
	 * first frame it draws afterwards as it would have been at once */
	g_garbleNow = g_render && g_garbleLeft > 0;
	if (g_garbleNow) g_garbleLeft--;
	g_framesRun++;
	if (g_spinFrame != 0 && g_framesRun == g_spinFrame)
		for (;;) g_spun++;
	if (g_cacheWorkFrame != 0 && g_framesRun == g_cacheWorkFrame) cache_work();
	slow_first();
	heap_churn();
	stop_once();
	if (g_watchedOpen) {
		mprotect(g_watchedPage, 4096, PROT_NONE);
		g_watchedOpen = 0;
	}
	if ((uint8_t)pad == 0xFF) {
		fprintf(stderr, "synth: all eight buttons at once - stopping on purpose\n");
		abort();
	}
	/* all but Up: the other way a core dies - a wild pointer, which arrives as
	 * a fault in the host (the vectored handler on Windows, SIGSEGV on Linux)
	 * rather than as a syscall */
	if ((uint8_t)pad == 0xFE) {
		volatile uintptr_t wild = 0x10;
		*(volatile uint32_t *)wild = 1;
	}
	if ((uint8_t)pad == 0xFD) g_dieIn = 1;
	if ((uint8_t)pad == 0xFB) g_dieIn = 2;
	if ((uint8_t)pad == 0xF7) g_dieIn = 3;
	synth_frame(g_synth, (uint8_t)pad);
}

/* Calls during a frame (issue 344): the host's function, in a callback slot,
 * called at each INPUT the machine executes - part way through the advance,
 * as a core that reports its input reads does. Invisible: it is the same fixed
 * slot address in every session, and no state carries it. */
extern void (*synth_on_input)(void);
static void input_polled(void) { if (g_inputPoll != 0) g_inputPoll(0, 0, 0, 0, 0, 0); }
ECL_EXPORT void SetInputPollCallback(uint64_t fn)
{
	g_inputPoll = (uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))(uintptr_t)fn;
	synth_on_input = g_inputPoll != 0 ? input_polled : 0;
}

/* An analog control's value, set before each frame. The synthetic game reads
 * none, so the values are only kept: the export is here so that a package
 * made from this one may declare analog controls (the engine refuses axes a
 * core cannot take). Invisible, and in no state, like the callback above. */
ECL_INVISIBLE static int32_t g_axes[8];
ECL_EXPORT void SetAxis(int32_t index, int32_t value)
{
	if (index >= 0 && index < 8) g_axes[index] = value;
}

/* The registers group (the debugger's register box): R0-R7, which a debugger
 * may set, and the frame counter, which it may only read. */
static const char *const g_registerNames[9] = {"R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "Frame"};
ECL_EXPORT int32_t GetRegisterCount(void) { return g_synth ? 9 : 0; }
ECL_EXPORT const char *GetRegisterName(int32_t i) { return i >= 0 && i < 9 ? g_registerNames[i] : 0; }
ECL_EXPORT int32_t GetRegisterBits(int32_t i) { (void)i; return 32; }
ECL_EXPORT int64_t GetRegisterValue(int32_t i)
{
	if (!g_synth || i < 0 || i > 8) return 0;
	return i == 8 ? (int64_t)synth_get_frame_counter(g_synth) : (int64_t)(uint32_t)synth_get_registers(g_synth)[i];
}
ECL_EXPORT void SetRegisterValue(int32_t i, int64_t v)
{
	if (g_synth && i >= 0 && i < 8) synth_get_registers(g_synth)[i] = (int32_t)v;
}

/* Guest-memory pointers the host reads while active (RAM/VRAM/audio domains). */
ECL_EXPORT uint8_t *GetRam(void)         { return synth_get_ram(g_synth); }
ECL_EXPORT uint8_t *GetFramebuffer(void) { return (uint8_t *)synth_get_framebuffer(g_synth); }
ECL_EXPORT int16_t *GetAudio(void)       { return (int16_t *)synth_get_audio(g_synth); }
ECL_EXPORT int      InputWasRead(void)   { die_if_armed(3, "InputWasRead"); return synth_input_was_read(g_synth); }

/* Save data (docs/save-data.md, the savedata group): two files the window's
 * Export Save Data writes out - the first 16 bytes of RAM as a card's slot,
 * and a note - so a frontend can take the whole export path. */
static const char g_saveNote[] = "gridWalker was here\n";
ECL_EXPORT int32_t GetSaveDataFileCount(void) { return g_synth ? 2 : 0; }
ECL_EXPORT const char *GetSaveDataFileName(int32_t i) { return i == 0 ? "card/slot1.sav" : i == 1 ? "notes.txt" : 0; }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) { return i == 0 ? 16 : i == 1 ? (int64_t)(sizeof g_saveNote - 1) : 0; }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i)
{
	return i == 0 ? synth_get_ram(g_synth) : i == 1 ? (const uint8_t *)g_saveNote : 0;
}

/* Turbo (optional guest ABI group): the host says nobody will look at the next
 * frames. The synth machine's picture IS its VRAM domain, which the witness
 * hashes, so the DRAWING is machine state and is never skipped; what this core
 * can skip is the palette resolution below, which exists only for the display.
 * Exporting it at all is deliberate: it puts the engine's turbo path - the
 * delta send, and the re-send after a state load - under every witness run. */
ECL_EXPORT void SetRenderingEnabled(int on) { g_render = on != 0; }

/* Palette-resolved presentation for the frontend's IVideoProvider (the raw
 * palette-index framebuffer is what the witness hashes; this is display only). */
ECL_INVISIBLE static uint32_t g_read;  /* pictures handed over while drawing, this session: in no state */

ECL_EXPORT uint32_t *GetVideoBgra(void)
{
	die_if_armed(1, "GetVideoBgra");
	if (g_render) g_read++;
	if (g_render) synth_get_video_bgra(g_synth, g_video);
	if (g_render && g_garbleNow)
		for (int i = 0; i < FB_W * FB_H; i++) g_video[i] ^= 0x00FFFFFFu ^ (uint32_t)(i * 2654435761u >> 8 & 0x00FFFFFFu);
	return g_video;
}

/* The picture's live size. The same as the package declares, so nothing reads
 * the picture differently; it is here so a package can declare more room than
 * the core draws, as a core with an internal resolution does (leg
 * B:box:picture-room). */
ECL_EXPORT int GetVideoWidth(void)  { return FB_W; }
ECL_EXPORT int GetVideoHeight(void) { return FB_H; }

/* --- self-described memory domains (guest ABI v1) ---
 * The generic Chimera adapter queries these AFTER Init, because a core's domain
 * sizes/count can depend on runtime settings (synth's are fixed, but the ABI is
 * uniform). Domain 0 is RAM (writable), domain 1 is VRAM (the palette-index
 * framebuffer, read-only). Domain 2, "Mailbox", is no memory of the machine's:
 * it is where a core puts what a game property points at when the value has
 * no place of its own (chimera#218). It is HIDDEN (GetMemoryDomainHidden) -
 * the engine reads properties through it, and no memory tool lists it. It
 * lives in memory that is in no state, and holds the four letters the Letter
 * property reads, followed by the three numbers of the StateSaving note
 * (above), by how many pictures the frontend has read, and by how many times
 * the fault handler ran for the Watched page (above), filled in whenever it
 * is asked for. The pictures read are what tell a frame that was read from
 * one that was only run (leg E:settle-probe, --settle-probe-unseen). Domain
 * 3, "Watched", is that page, hidden too. */
#define MD_COUNT 4
static const char *const md_names[MD_COUNT] = { "RAM", "VRAM", "Mailbox", "Watched" };
ECL_INVISIBLE static uint8_t g_mailbox[24];

ECL_EXPORT int GetMemoryDomainCount(void) { return MD_COUNT; }

ECL_EXPORT const char *GetMemoryDomainName(int i) { return (i >= 0 && i < MD_COUNT) ? md_names[i] : 0; }

ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) {
	if (i == 0) return synth_get_ram(g_synth);
	if (i == 1) return (uint8_t *)synth_get_framebuffer(g_synth);
	if (i == 2) {
		/* the four letters, then the StateSaving note: when last told, frames run, times told */
		memcpy(g_mailbox, "MAIL", 4);
		memcpy(g_mailbox + 4, &g_toldAt, 4);
		memcpy(g_mailbox + 8, &g_framesRun, 4);
		memcpy(g_mailbox + 12, &g_told, 4);
		memcpy(g_mailbox + 16, &g_read, 4);
		memcpy(g_mailbox + 20, &g_handlerRuns, 4);
		return g_mailbox;
	}
	if (i == 3) return g_watchedPage;
	return 0;
}

ECL_EXPORT int64_t GetMemoryDomainSize(int i) {
	if (i == 0) return 4096;
	if (i == 1) return FB_W * FB_H;
	if (i == 2) return sizeof g_mailbox;
	if (i == 3) return 4096;
	return 0;
}

ECL_EXPORT int GetMemoryDomainWritable(int i) { return i == 0 ? 1 : 0; }
ECL_EXPORT int GetMemoryDomainHidden(int i) { return i >= 2 ? 1 : 0; }

/* --- buses (engine.h, ce_session_bus_*) ---
 * An address space the core resolves itself: three times 64 KiB, so a bulk
 * read crosses the engine's chunks, each byte RAM mirrored and stirred by its
 * own address, so an offset wrong by any amount reads something else. "Bus"
 * answers runs through ReadBus; "Bus (peeks)" declines them, and the engine
 * peeks it a byte at a time - both must read what PeekBus says (witness leg
 * E:bus-read). */
#define BUS_SIZE 0x30000
static uint8_t bus_byte(int64_t addr)
{
	const uint8_t *ram = synth_get_ram(g_synth);
	return (uint8_t)(ram[addr & 4095] ^ (uint8_t)(addr >> 12) ^ (uint8_t)(addr * 7));
}

ECL_EXPORT int32_t GetBusCount(void) { return g_synth ? 2 : 0; }
ECL_EXPORT const char *GetBusName(int32_t b) { return b == 0 ? "Bus" : "Bus (peeks)"; }
ECL_EXPORT int64_t GetBusSize(int32_t b) { (void)b; return BUS_SIZE; }
ECL_EXPORT int32_t PeekBus(int32_t b, int32_t addr) { (void)b; return addr >= 0 && addr < BUS_SIZE ? bus_byte(addr) : 0; }

ECL_INVISIBLE static uint8_t g_busRun[65536];
ECL_EXPORT const uint8_t *ReadBus(int32_t b, int64_t addr, int32_t len)
{
	if (b != 0 || len < 0 || len > (int32_t)sizeof g_busRun) return 0;
	for (int32_t i = 0; i < len; i++) g_busRun[i] = addr + i >= 0 && addr + i < BUS_SIZE ? bus_byte(addr + i) : 0;
	return g_busRun;
}

/* Which of a bus is live (engine.h, ce_session_bus_ranges). Nothing on the
 * synth's bus is ever unmapped; it says three pieces are live anyway - out of
 * order, at addresses that are no multiple of four, the last running off the
 * end - so the witness can hold a search to them (leg E:bus-ranges). "Bus
 * (peeks)" has no word on it, which is "all of it". */
ECL_EXPORT const int64_t *GetBusRanges(int32_t b)
{
	static const int64_t live[] = { 0x20001, 0x0ff0, 0x1000, 0x0801, BUS_SIZE - 6, 64, 0, 0 };
	return b == 0 ? live : 0;
}

/* --- a game core's property table (docs/game-cores.md) ---
 * Names for places in RAM, which the frontend's tools watch, poke and freeze by
 * name and Lua reaches through game.*. The synth is an emulator, not a game
 * core; it carries a table anyway so the witness can drive the whole path - the
 * engine reading the export, the frontend checking it against the domains, and
 * the tools and scripts using it - without a game core in the tree. Status is
 * every test rom's convention (SPEC.md, "Test goals"); the rest is gridWalker's
 * layout (roms/gridWalker.sasm). */
/* --- the same, as a DYNAMIC table (engine.h, "A DYNAMIC table") ---
 * With the movingProperties setting on, the table is the core's answer for
 * now: properties that move and come and go as the game runs, which is what a
 * Flash movie's variables do on its emulator's heap. Nothing here is on a
 * heap; the cursor's row stands in for whatever makes a real one move:
 *   Wanderer   a byte at RAM 0x200 + 4 * row: it is somewhere else whenever
 *              the cursor changes row, and what was written at the old place
 *              stays behind there;
 *   Sometimes  a byte at RAM 0x240, there only while the row is odd;
 *   Mirror     a byte on "Bus", which has no pointer: read through the bus;
 *   Origin     the byte at RAM 0x200, which does not move: where Wanderer is
 *              while the row is 0, and what tells a write that followed
 *              Wanderer from one that went where it used to be;
 *   wanderer   a byte at RAM 0x244, which is not Wanderer: a game's own names
 *              are exact, and two that differ by case are two (chimera#218);
 *   Row, Steps where the others' places and values are worked out from.
 * The engine asks for the list (GetGameProperties) when told to, and for one
 * property by name (GetGameProperty) before every use; witness leg
 * E:dynamic-properties follows them through a whole game. The text is made in
 * memory that is in no state: asking must not change the machine. */
ECL_INVISIBLE static char g_propsNow[1536];
ECL_INVISIBLE static int g_moving; /* 0 not asked yet, 1 off, 2 on */

static int moving(void)
{
	if (g_moving == 0) g_moving = wbx_setting_long("movingProperties", 0) != 0 ? 2 : 1;
	return g_moving == 2;
}

static int moving_entry(char *out, size_t cap, const char *name)
{
	const uint8_t *ram = synth_get_ram(g_synth);
	const char *shape = "{ \"name\": \"%s\", \"domain\": \"%s\", \"offset\": %d, \"type\": \"%s\", \"group\": \"Moving\" }";
	if (!strcmp(name, "Wanderer")) return snprintf(out, cap, shape, name, "RAM", 0x200 + 4 * ram[2], "u8");
	if (!strcmp(name, "Sometimes") && (ram[2] & 1)) return snprintf(out, cap, shape, name, "RAM", 0x240, "u8");
	if (!strcmp(name, "Mirror")) return snprintf(out, cap, shape, name, "Bus", 4, "u8");
	if (!strcmp(name, "Origin")) return snprintf(out, cap, shape, name, "RAM", 0x200, "u8");
	if (!strcmp(name, "wanderer")) return snprintf(out, cap, shape, name, "RAM", 0x244, "u8");
	if (!strcmp(name, "Row")) return snprintf(out, cap, shape, name, "RAM", 2, "u8");
	if (!strcmp(name, "Steps")) return snprintf(out, cap, shape, name, "RAM", 4, "u32");
	return 0;
}

/* What this "game" calls a control, beside the name the package declares:
 * the witness checks it reaches the engine (ce_session_button_label). One
 * control has a name of its own; the rest have none, which is what "" says. */
ECL_EXPORT const char *GetButtonLabel(int32_t index) { return index == 4 ? "Jump" : ""; }

ECL_EXPORT const char *GetGameProperty(const char *name)
{
	g_propsNow[0] = 0;
	if (g_synth && moving() && name) moving_entry(g_propsNow, sizeof g_propsNow, name);
	return g_propsNow;
}

ECL_EXPORT const char *GetGameProperties(void)
{
	if (moving())
	{
		static const char *const names[] = { "Wanderer", "Sometimes", "Mirror", "Origin", "wanderer", "Row", "Steps" };
		size_t at = (size_t)snprintf(g_propsNow, sizeof g_propsNow, "{ \"dynamic\": true, \"properties\": [");
		int listed = 0;
		for (int k = 0; g_synth && k < (int)(sizeof names / sizeof names[0]); k++)
		{
			char one[256];
			if (moving_entry(one, sizeof one, names[k]) <= 0) continue;
			at += (size_t)snprintf(g_propsNow + at, sizeof g_propsNow - at, "%s%s", listed++ ? ", " : "", one);
		}
		snprintf(g_propsNow + at, sizeof g_propsNow - at, "] }");
		return g_propsNow;
	}
	/* the steps taken stand in for a game's own timer (docs/game-cores.md, "gameTimer") */
	return "{ \"gameTimer\": \"Steps\", \"properties\": ["
		"{ \"name\": \"Status\", \"domain\": \"RAM\", \"offset\": 0, \"type\": \"u8\", \"group\": \"Game\","
		" \"values\": { \"0\": \"Playing\", \"1\": \"Won\", \"2\": \"Lost\" },"
		" \"description\": \"Every test rom's result byte\" },"
		"{ \"name\": \"Cursor.X\", \"domain\": \"RAM\", \"offset\": 1, \"type\": \"u8\", \"group\": \"Cursor\","
		" \"description\": \"Column of the cell the cursor is in\" },"
		"{ \"name\": \"Cursor.Y\", \"domain\": \"RAM\", \"offset\": 2, \"type\": \"u8\", \"group\": \"Cursor\" },"
		"{ \"name\": \"Steps\", \"domain\": \"RAM\", \"offset\": 4, \"type\": \"u32\", \"group\": \"Game\","
		" \"description\": \"Moves made; past 1000 the game is lost\" },"
		"{ \"name\": \"Started\", \"domain\": \"RAM\", \"offset\": 8, \"type\": \"bool\", \"group\": \"Game\" },"
		/* every other kind of property, over RAM gridWalker leaves alone (0x100 on),
		 * so the witness can put each through the engine and read the bytes back */
		"{ \"name\": \"Test.Frames\", \"domain\": \"RAM\", \"offset\": 256, \"type\": \"u64\", \"group\": \"Test\" },"
		"{ \"name\": \"Test.Balance\", \"domain\": \"RAM\", \"offset\": 264, \"type\": \"s64\", \"group\": \"Test\" },"
		"{ \"name\": \"Test.Gravity\", \"domain\": \"RAM\", \"offset\": 272, \"type\": \"f64\", \"group\": \"Test\" },"
		"{ \"name\": \"Test.Name\", \"domain\": \"RAM\", \"offset\": 280, \"type\": \"string\", \"length\": 8, \"group\": \"Test\" },"
		"{ \"name\": \"Test.Wide\", \"domain\": \"RAM\", \"offset\": 288, \"type\": \"string\", \"length\": 8,"
		" \"encoding\": \"utf16le\", \"group\": \"Test\" },"
		"{ \"name\": \"Test.Key\", \"domain\": \"RAM\", \"offset\": 296, \"type\": \"bytes\", \"length\": 4, \"group\": \"Test\" },"
		"{ \"name\": \"Test.Score\", \"domain\": \"RAM\", \"offset\": 300, \"type\": \"u32\", \"endian\": \"big\", \"group\": \"Test\" },"
		"{ \"name\": \"Test.Row\", \"domain\": \"RAM\", \"offset\": 304, \"type\": \"s16\", \"count\": 4, \"stride\": 4, \"group\": \"Test\" },"
		"{ \"name\": \"Test.Col\", \"domain\": \"RAM\", \"offset\": 306, \"type\": \"u8\", \"count\": 4, \"stride\": 4, \"group\": \"Test\" },"
		"{ \"name\": \"Test.Flag\", \"domain\": \"RAM\", \"offset\": 320, \"type\": \"u8\", \"bit\": 2, \"bits\": 1, \"group\": \"Test\" },"
		"{ \"name\": \"Test.Nibble\", \"domain\": \"RAM\", \"offset\": 320, \"type\": \"u8\", \"bit\": 4, \"bits\": 4, \"group\": \"Test\" },"
		/* a value with no place of its own, read through the hidden domain */
		"{ \"name\": \"Letter\", \"domain\": \"Mailbox\", \"offset\": 0, \"type\": \"string\", \"length\": 4, \"group\": \"Test\","
		" \"description\": \"What the core left in its mailbox\" }"
		"] }";
}

/* A movie made elsewhere, as this core reads it (engine.h, ce_import_movie):
 * called INSTEAD of Init. The synth's "foreign" movie is its own movie text -
 * one "|UDLRABsS|" line per frame - mounted as "movie", and the game it was
 * made on is named by the importRom option and mounted under that name. The
 * answer is what a real importer answers - the settings, firmware and files
 * the movie dictates, notes, and the input as Chimera's log - so the witness
 * and the frontend can take the whole path without a core that imports
 * anything real. An importFill option stands in for a setting a movie can
 * dictate. */
static char g_import[64 * 1024];

ECL_EXPORT const char *ImportMovie(void)
{
	FILE *f = fopen("movie", "rb");
	if (!f) return "{\"error\": \"no movie: the file \\\"movie\\\" is not mounted\"}";
	static char text[48 * 1024];
	size_t n = fread(text, 1, sizeof text - 1, f);
	fclose(f);
	text[n] = 0;
	char rom[128] = "";
	wbx_setting_str("importRom", rom, sizeof rom);
	if (rom[0] == 0) return "{\"error\": \"which game was this movie made on? (importRom)\"}";
	FILE *g = fopen(rom, "rb");
	if (!g) {
		snprintf(g_import, sizeof g_import, "{\"error\": \"the game %s is not at hand\"}", rom);
		return g_import;
	}
	fclose(g);
	const long fill = wbx_setting_long("importFill", 0);

	size_t o = (size_t)snprintf(g_import, sizeof g_import,
		"{\"format\": \"Synth movie\", \"settings\": {\"initFillByte\": %ld}, \"firmware\": [],"
		" \"files\": [{\"name\": \"%s\", \"slot\": \"rom\"}],"
		" \"notes\": [\"imported by the synth core\"], \"input\": \"[Input]\\n", fill, rom);
	long frames = 0;
	for (const char *line = text; *line && o + 32 < sizeof g_import; ) {
		const char *end = line;
		while (*end && *end != '\n' && *end != '\r') end++;
		if (*line == '|' && end > line) {
			for (const char *c = line; c < end && o + 16 < sizeof g_import; c++) g_import[o++] = *c;
			o += (size_t)snprintf(g_import + o, sizeof g_import - o, "\\n");
			frames++;
		}
		line = end;
		while (*line == '\n' || *line == '\r') line++;
	}
	snprintf(g_import + o, sizeof g_import - o, "[/Input]\\n\", \"frames\": %ld}", frames);
	return g_import;
}

/* What this core would choose for a game before a project exists
 * (ce_suggest_settings): called INSTEAD of Init, for a package that declares
 * "suggestSettings". The synth suggests one value, with a note saying where
 * it comes from, so a frontend's suggestion step can be tested. The note
 * also says what it was shown - the disks in the slot map, a cue's track,
 * the disk speed chosen so far - since a core is asked with the files and
 * settings picked so far. And the game has a setting of its own and, with
 * that setting on, a firmware file of its own; each answer also repeats a
 * package setting and a package firmware, which a frontend drops. A game
 * whose file ends in NOFILL gets none of it: nothing suggested, nothing of
 * its own - so a frontend can be seen taking back another game's. */
static char g_suggestion[2048];
ECL_EXPORT const char *SuggestSettings(void)
{
	uint32_t romLen = 0;
	uint8_t *rom = read_rom(&romLen);
	const int nofill = rom && romLen >= 6 && memcmp(rom + romLen - 6, "NOFILL", 6) == 0;
	free(rom);
	if (nofill) return "{\"values\": {}, \"note\": \"The synthetic core suggests nothing for this game.\"}";
	char slots[512] = "";
	FILE *f = fopen("slots", "rb");
	if (f) {
		size_t n = fread(slots, 1, sizeof slots - 1, f);
		slots[n] = 0;
		fclose(f);
	}
	char seen[160] = "";
	const char *disks = strstr(slots, "\"disk\"");
	if (disks) {
		int count = 0;
		for (const char *p = strchr(disks, '['); p && *p && *p != ']'; p++)
			if (*p == '.') count++;
		FILE *track = fopen("disk1.bin", "rb");
		if (track) fclose(track);
		snprintf(seen, sizeof seen, " It sees %d disk%s%s, at disk speed %ld.", count, count == 1 ? "" : "s",
			track ? " and the track of disk1.cue" : "", wbx_setting_long("diskSpeed", 1));
	}
	snprintf(g_suggestion, sizeof g_suggestion,
		"{\"values\": {\"initFillByte\": 9}, \"note\": \"The synthetic core suggests a fill of 9.%s\","
		" \"settings\": [{\"name\": \"gameLogo\", \"display\": \"Game Logo\", \"type\": \"enum\","
		" \"options\": [\"off\", \"on\"], \"default\": \"off\", \"sync\": true,"
		" \"description\": \"This game's own: shows its logo, from a file of its own.\"},"
		" {\"name\": \"initFillByte\", \"display\": \"Not the package's\", \"type\": \"int\", \"default\": 1}],"
		" \"firmware\": [{\"id\": \"gamelogo\", \"display\": \"Game logo\", \"name\": \"gamelogo.bin\","
		" \"requiredWhen\": {\"setting\": \"gameLogo\", \"is\": \"on\"}},"
		" {\"id\": \"bios\", \"display\": \"Not the package's\", \"name\": \"other.bin\", \"required\": true}]}",
		seen);
	return g_suggestion;
}

int main(void) { return 0; }
