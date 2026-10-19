/* test_gl_session.cpp - what a session made through the GL bridge goes with it.
 *
 * The bridge's context used to live for the whole process: no core tears its
 * renderer down when its sandbox is destroyed, so every object a closed
 * session made stayed in the driver, a project's worth with each one opened.
 * Here a buffer is made and bound the way a guest does it, through the
 * dispatcher; the session ends; and in the next session's context the name
 * names nothing. Needs a context (Mesa's EGL here, with or without a GPU),
 * and says SKIP without one.
 */

#include "chimera/engine.h"
#include <glad/gl.h>
#include "gl-bridge-ops.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" int32_t ce_gl_start(char *error_out, int32_t error_len);
extern "C" void ce_gl_session_end(void);
extern "C" void ce_gl_release(void);
#ifdef _WIN32
extern "C" uintptr_t __attribute__((sysv_abi)) ce_gl_dispatch(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
#else
extern "C" uintptr_t ce_gl_dispatch(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
#endif

static uintptr_t call(uintptr_t op, void *args) { return ce_gl_dispatch(op, reinterpret_cast<uintptr_t>(args), 0, 0, 0, 0); }

extern "C" void ce_gl_state_loaded(int64_t to);
extern "C" uint64_t ce_gl_contexts_renewed(void);
extern "C" uint64_t ce_gl_readback_overruns(void);
extern "C" void ce_gl_guest_check(int (*can)(void *, uintptr_t, uintptr_t, int), void *ctx);

/* A framebuffer of W x H RGBA8 pixels, all one colour, bound for reading. */
static void colourTarget(int W, int H, float r, float g, float b)
{
	GLuint tex = 0, fbo = 0;
	ChimeraGlArgs_glGenTextures gt{ 1, &tex }; call(CHIMERA_GL_OP_glGenTextures, &gt);
	ChimeraGlArgs_glBindTexture bt{ GL_TEXTURE_2D, tex }; call(CHIMERA_GL_OP_glBindTexture, &bt);
	ChimeraGlArgs_glTexImage2D ti{ GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr };
	call(CHIMERA_GL_OP_glTexImage2D, &ti);
	ChimeraGlArgs_glGenFramebuffers gf{ 1, &fbo }; call(CHIMERA_GL_OP_glGenFramebuffers, &gf);
	ChimeraGlArgs_glBindFramebuffer bf{ GL_FRAMEBUFFER, fbo }; call(CHIMERA_GL_OP_glBindFramebuffer, &bf);
	ChimeraGlArgs_glFramebufferTexture2D ft{ GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 };
	call(CHIMERA_GL_OP_glFramebufferTexture2D, &ft);
	ChimeraGlArgs_glClearColor cc{ r, g, b, 1.0f }; call(CHIMERA_GL_OP_glClearColor, &cc);
	ChimeraGlArgs_glClear cl{ GL_COLOR_BUFFER_BIT }; call(CHIMERA_GL_OP_glClear, &cl);
}

static void packRowLength(int n)
{
	ChimeraGlArgs_glPixelStorei ps{ GL_PACK_ROW_LENGTH, n };
	call(CHIMERA_GL_OP_glPixelStorei, &ps);
}

static int refuseEverything(void *, uintptr_t, uintptr_t, int) { return 0; }

static GLsync fence()
{
	ChimeraGlArgs_glFenceSync a{ GL_SYNC_GPU_COMMANDS_COMPLETE, 0 };
	return reinterpret_cast<GLsync>(ce_gl_dispatch(CHIMERA_GL_OP_glFenceSync, reinterpret_cast<uintptr_t>(&a), 0, 0, 0, 0));
}

static bool isSync(GLsync name)
{
	ChimeraGlArgs_glIsSync a{ name };
	return ce_gl_dispatch(CHIMERA_GL_OP_glIsSync, reinterpret_cast<uintptr_t>(&a), 0, 0, 0, 0) != 0;
}

static bool isTexture(GLuint name)
{
	ChimeraGlArgs_glIsTexture a{ name };
	const bool is = call(CHIMERA_GL_OP_glIsTexture, &a) != 0;
	ce_gl_release();
	return is;
}

/* a texture with storage, under a new name, the way a guest makes one */
static GLuint texture()
{
	GLuint name = 0;
	ChimeraGlArgs_glGenTextures gt{ 1, &name }; call(CHIMERA_GL_OP_glGenTextures, &gt);
	ChimeraGlArgs_glBindTexture bt{ GL_TEXTURE_2D, name }; call(CHIMERA_GL_OP_glBindTexture, &bt);
	ChimeraGlArgs_glTexImage2D ti{ GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr };
	call(CHIMERA_GL_OP_glTexImage2D, &ti);
	return name;
}

static bool isBuffer(GLuint name)
{
	ChimeraGlArgs_glIsBuffer a{ name };
	const bool is = call(CHIMERA_GL_OP_glIsBuffer, &a) != 0;
	ce_gl_release();
	return is;
}

int main()
{
	char err[256] = "";
	if (ce_gl_start(err, (int32_t)sizeof err) == 0)
	{
		std::printf("test_gl_session: SKIP (no GL context here: %s)\n", err);
		return 0;
	}
	GLuint name = 0;
	ChimeraGlArgs_glGenBuffers gen{ 1, &name };
	call(CHIMERA_GL_OP_glGenBuffers, &gen);
	ChimeraGlArgs_glBindBuffer bind{ GL_ARRAY_BUFFER, name };
	call(CHIMERA_GL_OP_glBindBuffer, &bind);
	ce_gl_release();
	assert(name != 0 && isBuffer(name));

	ce_gl_session_end();
	assert(ce_gl_start(err, (int32_t)sizeof err) != 0);
	assert(!isBuffer(name));   /* the new session's context has none of the old one's objects */

	/* two sessions holding it: the first to end leaves it to the second */
	assert(ce_gl_start(err, (int32_t)sizeof err) != 0);
	gen = ChimeraGlArgs_glGenBuffers{ 1, &name };
	call(CHIMERA_GL_OP_glGenBuffers, &gen);
	bind = ChimeraGlArgs_glBindBuffer{ GL_ARRAY_BUFFER, name };
	call(CHIMERA_GL_OP_glBindBuffer, &bind);
	ce_gl_release();
	ce_gl_session_end();
	assert(isBuffer(name));
	ce_gl_session_end();

	/* A load leaves fences no restored guest knows of; once signalled they
	 * are deleted at the next fence, and a name for one answers as a fence
	 * that is over. */
	assert(ce_gl_start(err, (int32_t)sizeof err) != 0);
	const GLsync before = fence();
	call(CHIMERA_GL_OP_glFinish, nullptr);
	const GLsync second = fence();
	call(CHIMERA_GL_OP_glFinish, nullptr);
	assert(before != nullptr && second != nullptr && isSync(before) && isSync(second));
	ce_gl_release();
	ce_gl_state_loaded(5);
	const GLsync after = fence();
	assert(after != nullptr && isSync(after));
	assert(!isSync(before) && !isSync(second));
	ce_gl_release();
	ce_gl_session_end();

	/* ---- a load that makes the core rebuild gives it a fresh context
	 * (2026-10-19): the old objects go with the old context, and every name
	 * the core still holds is taken in the new one, empty, so no new object
	 * gets a number a stale call in the restored state could reach ---- */
	assert(ce_gl_start(err, (int32_t)sizeof err) != 0);
	{
		const GLuint kept1 = texture(), dropped = texture(), kept2 = texture();
		ChimeraGlArgs_glDeleteTextures del{ 1, &dropped };
		call(CHIMERA_GL_OP_glDeleteTextures, &del);   /* the core let this one go before the load */
		const GLuint program = (GLuint)ce_gl_dispatch(CHIMERA_GL_OP_glCreateProgram, 0, 0, 0, 0, 0);
		ce_gl_release();
		assert(kept1 != 0 && kept2 != 0 && program != 0 && isTexture(kept1) && isTexture(kept2));

		const uint64_t renewed = ce_gl_contexts_renewed();
		ce_gl_state_loaded(7);
		assert(ce_gl_contexts_renewed() == renewed + 1);
		/* the objects went with the context: their names name nothing now */
		assert(!isTexture(kept1) && !isTexture(kept2));
		/* new objects never get a name the core holds; the one it let go may come back */
		GLuint fresh[4] = { 0, 0, 0, 0 };
		for (GLuint &f : fresh)
		{
			f = texture();
			std::printf("test_gl_session: after the load a new texture is %u (the core holds %u and %u)\n",
				(unsigned)f, (unsigned)kept1, (unsigned)kept2);
			assert(f != 0 && f != kept1 && f != kept2);
		}
		const GLuint program2 = (GLuint)ce_gl_dispatch(CHIMERA_GL_OP_glCreateProgram, 0, 0, 0, 0, 0);
		assert(program2 != 0 && program2 != program);
		/* a stale delete of a name the core held leaves the new objects alone */
		ChimeraGlArgs_glDeleteTextures stale{ 1, &kept1 };
		call(CHIMERA_GL_OP_glDeleteTextures, &stale);
		ce_gl_release();
		for (GLuint f : fresh) assert(isTexture(f));
	}
	ce_gl_session_end();
	/* two sessions holding the context: a load keeps it, with the other's objects */
	assert(ce_gl_start(err, (int32_t)sizeof err) != 0);
	assert(ce_gl_start(err, (int32_t)sizeof err) != 0);
	{
		const GLuint other = texture();
		ce_gl_release();
		const uint64_t renewed = ce_gl_contexts_renewed();
		ce_gl_state_loaded(9);
		assert(ce_gl_contexts_renewed() == renewed && isTexture(other));
	}
	ce_gl_session_end();
	ce_gl_session_end();

	/* ---- readbacks, functions a driver lacks, and memory the core does not
	 * have (2026-10-18) ---- */
	assert(ce_gl_start(err, (int32_t)sizeof err) != 0);
	{
		/* rows that overlap (a row length smaller than the width): the guard's
		 * buffer is big enough for what GL writes - the driver wrote past it,
		 * into Chimera's heap, which the guard counts as an overrun */
		const int W = 8192, H = 2;
		colourTarget(W, H, 0.25f, 0.5f, 0.75f);
		packRowLength(1);
		const size_t writes = 16 * (H - 1) + 16 * (size_t)W;   /* RGBA floats: 16 bytes a pixel */
		std::vector<uint8_t> out(writes + 64, 0xAB);
		const uint64_t overrunsBefore = ce_gl_readback_overruns();
		ChimeraGlArgs_glReadPixels rp{ 0, 0, W, H, GL_RGBA, GL_FLOAT, out.data() };
		call(CHIMERA_GL_OP_glReadPixels, &rp);
		std::printf("test_gl_session: overlapping rows, %llu overrun(s)\n",
			(unsigned long long)(ce_gl_readback_overruns() - overrunsBefore));
		assert(ce_gl_readback_overruns() == overrunsBefore);
		for (size_t i = writes; i < out.size(); i++) assert(out[i] == 0xAB);   /* nothing past it */
		packRowLength(0);
		ce_gl_release();

		/* rows with room between them (a row length larger than the width): the
		 * bytes between rows are the guest's and stay so - they used to get
		 * what an earlier readback left in the guard's buffer */
		colourTarget(32, 32, 1.0f, 0.0f, 0.0f);
		std::vector<uint8_t> red(32 * 32 * 4);
		ChimeraGlArgs_glReadPixels first{ 0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, red.data() };
		call(CHIMERA_GL_OP_glReadPixels, &first);
		colourTarget(4, 4, 0.0f, 0.0f, 1.0f);
		packRowLength(8);
		std::vector<uint8_t> gaps(32 * 4, 0xAB);   /* stride 32 bytes, 16 of them a row */
		ChimeraGlArgs_glReadPixels rows{ 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, gaps.data() };
		call(CHIMERA_GL_OP_glReadPixels, &rows);
		for (int r = 0; r < 4; r++)
		{
			for (int x = 0; x < 4; x++) assert(gaps[r * 32 + x * 4 + 2] == 255 && gaps[r * 32 + x * 4] == 0);   /* blue */
			if (r < 3) for (int k = 16; k < 32; k++) assert(gaps[r * 32 + k] == 0xAB);
		}
		packRowLength(0);
		ce_gl_release();

		/* the same through glReadnPixels and glGetTextureSubImage, which the
		 * guard did not cover: the gaps keep the guest's bytes */
		colourTarget(32, 32, 1.0f, 0.0f, 0.0f);
		call(CHIMERA_GL_OP_glReadPixels, &first);
		colourTarget(4, 4, 0.0f, 1.0f, 0.0f);
		packRowLength(8);
		std::vector<uint8_t> nGaps(32 * 4, 0xAB);
		ChimeraGlArgs_glReadnPixels rn{ 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)nGaps.size(), nGaps.data() };
		call(CHIMERA_GL_OP_glReadnPixels, &rn);
		for (int r = 0; r < 4; r++)
		{
			for (int x = 0; x < 4; x++) assert(nGaps[r * 32 + x * 4 + 1] == 255 && nGaps[r * 32 + x * 4] == 0);   /* green */
			if (r < 3) for (int k = 16; k < 32; k++) assert(nGaps[r * 32 + k] == 0xAB);
		}
		GLint boundTex = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex);
		std::vector<uint8_t> tGaps(32 * 4, 0xAB);
		ChimeraGlArgs_glGetTextureSubImage ts{ (GLuint)boundTex, 0, 0, 0, 0, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE,
			(GLsizei)tGaps.size(), tGaps.data() };
		call(CHIMERA_GL_OP_glGetTextureSubImage, &ts);
		for (int r = 0; r < 4; r++)
		{
			for (int x = 0; x < 4; x++) assert(tGaps[r * 32 + x * 4 + 1] == 255);
			if (r < 3) for (int k = 16; k < 32; k++) assert(tGaps[r * 32 + k] == 0xAB);
		}
		packRowLength(0);
		ce_gl_release();

		/* a function the driver did not supply, hand-written case and generated:
		 * refused with 0, where calling through the null pointer ended the process */
		auto storage = glad_glBufferStorage;
		glad_glBufferStorage = nullptr;
		ChimeraGlArgs_glBufferStorage bs{ GL_ARRAY_BUFFER, 16, nullptr, 0 };
		assert(call(CHIMERA_GL_OP_glBufferStorage, &bs) == 0);
		glad_glBufferStorage = storage;
		auto blend = glad_glBlendColor;
		glad_glBlendColor = nullptr;
		ChimeraGlArgs_glBlendColor bc{ 0.0f, 0.0f, 0.0f, 0.0f };
		assert(call(CHIMERA_GL_OP_glBlendColor, &bc) == 0);
		glad_glBlendColor = blend;

		/* memory the core does not have: a buffer upload and a readback into it
		 * are refused, as the sandbox answers (here: nothing is the core's) */
		GLuint buf = 0;
		ChimeraGlArgs_glGenBuffers gb{ 1, &buf }; call(CHIMERA_GL_OP_glGenBuffers, &gb);
		ChimeraGlArgs_glBindBuffer bb{ GL_ARRAY_BUFFER, buf }; call(CHIMERA_GL_OP_glBindBuffer, &bb);
		uint8_t bytes[16] = { 1, 2, 3 };
		ce_gl_guest_check(refuseEverything, nullptr);
		ChimeraGlArgs_glBufferData bd{ GL_ARRAY_BUFFER, 16, bytes, GL_STATIC_DRAW };
		call(CHIMERA_GL_OP_glBufferData, &bd);
		std::vector<uint8_t> untouched(4 * 4 * 4, 0xAB);
		ChimeraGlArgs_glReadPixels into{ 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, untouched.data() };
		call(CHIMERA_GL_OP_glReadPixels, &into);
		/* and the three readbacks the guard covers since 2026-10-18 */
		ChimeraGlArgs_glReadnPixels intoN{ 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)untouched.size(), untouched.data() };
		call(CHIMERA_GL_OP_glReadnPixels, &intoN);
		GLint texNow = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &texNow);
		ChimeraGlArgs_glGetTextureSubImage intoT{ (GLuint)texNow, 0, 0, 0, 0, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE,
			(GLsizei)untouched.size(), untouched.data() };
		call(CHIMERA_GL_OP_glGetTextureSubImage, &intoT);
		GLuint named = 0;
		ChimeraGlArgs_glCreateBuffers cb{ 1, &named };
		call(CHIMERA_GL_OP_glCreateBuffers, &cb);
		glNamedBufferData(named, 16, "0123456789abcdef", GL_STATIC_DRAW);
		ChimeraGlArgs_glGetNamedBufferSubData intoB{ named, 0, 16, untouched.data() };
		call(CHIMERA_GL_OP_glGetNamedBufferSubData, &intoB);
		ce_gl_guest_check(nullptr, nullptr);
		GLint size = -1;
		ChimeraGlArgs_glGetBufferParameteriv gp{ GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size };
		call(CHIMERA_GL_OP_glGetBufferParameteriv, &gp);
		assert(size == 0);                                   /* the upload was not made */
		for (uint8_t v : untouched) assert(v == 0xAB);       /* nor the readback */
		ce_gl_release();
	}
	ce_gl_session_end();

	std::printf("test_gl_session: ok\n");
	return 0;
}
