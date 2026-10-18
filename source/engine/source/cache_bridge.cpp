// The host's side of the compile-cache bridge (miniBox source/cache/
// cache-bridge.h): a directory of files a core names, kept between sessions.
// The core promises the bytes are a pure function of its inputs; this keeps
// them. Names are relative paths with '/' between folders; an absolute
// prefix, a '..' segment, a backslash or a name Windows gives a meaning is
// refused. A store writes beside and renames, so a reader never sees half.
#include "chimera/engine.h"
#include "cache-bridge.h"
#include "file_io.hpp"
#include "progress.hpp"
#include "sha1.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <filesystem>
#include <string>
#include <system_error>

namespace
{
std::string s_dir;
uint64_t s_fetched, s_stored;

/* whether each object is announced on stdout with its hash: only a precompile
 * session has anyone reading (the wizard, collecting a game's manifest), and
 * hashing everything a core boots from costs seconds on a CPU without the SHA
 * instructions. CHIMERA_CACHE_ANNOUNCE=1 asks for it in any session. */
bool s_announce;

/* the relative name the caller gave, once it has been found acceptable */
std::string s_rel;

/* One folder or file name of a cache name. The core makes the name, perhaps
 * from what a game image says, so every way a name could reach something other
 * than a file in the cache is refused: a segment of dots (".." climbs out), a
 * trailing dot or space (Windows drops them, so two names could reach one
 * file), control characters, and the Windows device names (CON, NUL, COM1,
 * ...), which open a device whatever extension follows them. */
bool segment_ok(const std::string &seg)
{
	if (seg.empty() || seg.back() == '.' || seg.back() == ' ') return false;
	for (char c : seg)
	{
		if (static_cast<unsigned char>(c) < 0x20) return false;
	}
	std::string stem = seg.substr(0, seg.find('.'));
	while (!stem.empty() && stem.back() == ' ') stem.pop_back();
	for (char &c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
	for (const char *d : devices)
	{
		if (stem == d) return false;
	}
	if (stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0)
		&& std::isdigit(static_cast<unsigned char>(stem[3])))
	{
		return false;
	}
	return true;
}

std::string safe_path(const char *name, uint64_t len)
{
	if (!name || !len || len > 1024) return {};
	std::string rel(name, static_cast<size_t>(len));
	/* a backslash is a folder separator on Windows, so ..\x would climb out */
	if (rel.find_first_of(":\\") != std::string::npos || rel.find('\0') != std::string::npos) return {};
	size_t start = 0;
	while (start <= rel.size())
	{
		size_t end = rel.find('/', start);
		if (end == std::string::npos) end = rel.size();
		if (!segment_ok(rel.substr(start, end - start))) return {};
		start = end + 1;
	}
	s_rel = rel;
	return s_dir + "/" + rel;
}

void make_dirs(const std::string &full)
{
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::u8path(full).parent_path(), ec);
}
}  // namespace

extern "C" void ce_cache_dir(const char *dir)
{
	s_dir = dir ? dir : "";
	while (!s_dir.empty() && (s_dir.back() == '/' || s_dir.back() == '\\')) s_dir.pop_back();
	// the whole chain: the caller names a directory by core and package
	std::error_code ec;
	if (!s_dir.empty()) std::filesystem::create_directories(std::filesystem::u8path(s_dir), ec);
}

extern "C" const char *ce_cache_dir_get(void)
{
	return s_dir.c_str();
}

/* said by the session as it opens (session.cpp) */
extern "C" void ce_cache_announce(int32_t precompile)
{
	const char *env = getenv("CHIMERA_CACHE_ANNOUNCE");
	s_announce = precompile != 0 || (env != nullptr && env[0] == '1');
}

#if defined(_WIN32) && defined(__GNUC__)
#define CE_BRIDGE_ABI __attribute__((sysv_abi))
#else
#define CE_BRIDGE_ABI
#endif

// The work, in the host's own ABI: a sysv-ABI function may carry no C++
// unwind data under mingw (".seh_handlerdata used outside of .seh_proc"),
// and std::string has destructors.
static __attribute__((noinline)) uintptr_t dispatch_impl(uintptr_t op, uintptr_t a, uintptr_t b)
{
	if (s_dir.empty()) return 0;
	switch (op)
	{
	case CACHE_OP_FETCH:
	{
		auto *args = reinterpret_cast<CacheFetchArgs *>(a);
		const std::string full = safe_path(reinterpret_cast<const char *>(static_cast<uintptr_t>(args->name)), args->name_len);
		if (full.empty()) return 0;
		const std::string rel = s_rel;
		FILE *f = chimera::openFile(full.c_str(), "rb");
		if (!f) return 0;
		/* the 64-bit calls: Windows' long is 32 bits */
#ifdef _WIN32
		_fseeki64(f, 0, SEEK_END);
		const int64_t size = _ftelli64(f);
#else
		fseeko(f, 0, SEEK_END);
		const int64_t size = ftello(f);
#endif
		if (size <= 0) { fclose(f); return 0; }
		if (args->dst && args->cap >= static_cast<uint64_t>(size))
		{
			rewind(f);
			if (fread(reinterpret_cast<void *>(static_cast<uintptr_t>(args->dst)), 1, static_cast<size_t>(size), f) != static_cast<size_t>(size)) { fclose(f); return 0; }
			s_fetched++;
			chimera::progress("loading compiled code", s_fetched, 0);
			/* one line per object, for whoever is watching this process fill a
			 * cache (the wizard's precompile step reads exactly these) */
			if (s_announce)
			{
				printf("[cache] fetched %s %s\n", rel.c_str(),
					chimera::sha1Hex(reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(args->dst)), static_cast<uint64_t>(size)).c_str());
				fflush(stdout);
			}
		}
		fclose(f);
		return static_cast<uintptr_t>(size);
	}
	case CACHE_OP_PROGRESS:
	{
		/* said by the guest as it works, because it cannot be asked meanwhile */
		static uintptr_t lastDone = SIZE_MAX, lastTotal = SIZE_MAX;
		if (a != lastDone || b != lastTotal)
		{
			lastDone = a;
			lastTotal = b;
			printf("Precompiled %llu/%llu modules\n", (unsigned long long)a, (unsigned long long)b);
			fflush(stdout);
		}
		return 1;
	}
	case CACHE_OP_STORE:
	{
		auto *args = reinterpret_cast<CacheStoreArgs *>(a);
		const std::string full = safe_path(reinterpret_cast<const char *>(static_cast<uintptr_t>(args->name)), args->name_len);
		if (full.empty() || !args->size) return 0;
		const std::string rel = s_rel;
		make_dirs(full);
		const std::string tmp = full + ".part";
		FILE *f = chimera::openFile(tmp.c_str(), "wb");
		if (!f) return 0;
		bool ok = fwrite(reinterpret_cast<const void *>(static_cast<uintptr_t>(args->data)), 1, static_cast<size_t>(args->size), f) == static_cast<size_t>(args->size);
		ok = fclose(f) == 0 && ok;
		if (!ok || !chimera::replaceFile(tmp.c_str(), full.c_str())) { chimera::removeFile(tmp.c_str()); return 0; }
		s_stored++;
		chimera::progress("keeping compiled code", s_stored, 0);
		if (s_announce)
		{
			printf("[cache] stored %s %s\n", rel.c_str(),
				chimera::sha1Hex(reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(args->data)), args->size).c_str());
			fflush(stdout);
		}
		return 1;
	}
	}
	return 0;
}

// entered from guest code: sysv on every host, the sandbox's callback shape
extern "C" uintptr_t CE_BRIDGE_ABI ce_cache_dispatch(uintptr_t op, uintptr_t a, uintptr_t b, uintptr_t, uintptr_t, uintptr_t)
{
	return dispatch_impl(op, a, b);
}

extern "C" uint64_t ce_cache_host_stored(void) { return s_stored; }
extern "C" uint64_t ce_cache_host_fetched(void) { return s_fetched; }
