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

	std::printf("test_gl_session: ok\n");
	return 0;
}
