/* zstd_dyn.hpp - libzstd, loaded at runtime.
 *
 * The build already ships libzstd beside libchimera (build/dll on both
 * targets); loading it at runtime from this library's own directory keeps the
 * engine free of per-platform link plumbing. Absence is a reported error, not
 * a crash.
 */

#ifndef CHIMERA_ZSTD_DYN_HPP
#define CHIMERA_ZSTD_DYN_HPP

#include <cstddef>

namespace chimera {

struct ZstdApi
{
	size_t (*compressBound)(size_t srcSize);
	size_t (*compress)(void *dst, size_t dstCap, const void *src, size_t srcSize, int level);
	unsigned (*isError)(size_t code);
	void *(*createDStream)(void);
	size_t (*initDStream)(void *zds);
	size_t (*freeDStream)(void *zds);
	struct Buffer { const void *ptr; size_t size; size_t pos; };
	struct OutBuffer { void *ptr; size_t size; size_t pos; };
	size_t (*decompressStream)(void *zds, OutBuffer *output, Buffer *input);
	/* streaming compression, so a big lump can say how far it is */
	void *(*createCStream)(void);
	size_t (*initCStream)(void *zcs, int level);
	size_t (*freeCStream)(void *zcs);
	/* ZSTD_compressStream2: endOp 0 continue, 2 end */
	size_t (*compressStream2)(void *zcs, OutBuffer *output, Buffer *input, int endOp);
	/* ZSTD_CCtx_setParameter; null on a libzstd too old to have it */
	size_t (*cctxSetParameter)(void *cctx, int param, int value);
};

/* A content checksum in every frame a stream writes from here on: damage to a
 * stored state then fails to decode instead of restoring a wrong machine
 * (2026-10-18). Any zstd reads such a frame; older frames still decode. */
inline void zstdChecksum(const ZstdApi *z, void *cctx)
{
	if (z != nullptr && z->cctxSetParameter != nullptr && cctx != nullptr)
		z->cctxSetParameter(cctx, 201 /* ZSTD_c_checksumFlag */, 1);
}

/* The loaded API, or nullptr with *error set to why. Loads once, then cached. */
const ZstdApi *zstdApi(const char **error);

} // namespace chimera

#endif
