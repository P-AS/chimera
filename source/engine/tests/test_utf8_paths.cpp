/* test_utf8_paths.cpp - the files the engine makes, under a folder whose name
 * is not plain ASCII.
 *
 * The engine takes UTF-8 paths everywhere. On Windows the C library reads a
 * narrow path in the ANSI code page instead, so a path with an accent names a
 * different, missing file: a Windows user whose name has one (the data folder
 * is under the user's profile) had no input journal and no compile cache.
 * This pins the input journal, the compile cache, the media maker and the
 * project file under such a folder. It is built for Linux with the other
 * tests, and for Windows by hand (meson compile -C build/meson-windows
 * test_utf8_paths) to be run there.
 *
 * Also pinned: the compile cache refuses names that reach outside it or name
 * a Windows device, and a media pack over a folder it cannot read fails with
 * an error instead of throwing through the C API.
 */

#include "chimera/engine.h"
#include "cache-bridge.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

#if defined(_WIN32) && defined(__GNUC__)
#define CE_BRIDGE_ABI __attribute__((sysv_abi))
#else
#define CE_BRIDGE_ABI
#endif
extern "C" uintptr_t CE_BRIDGE_ABI ce_cache_dispatch(uintptr_t op, uintptr_t a, uintptr_t b, uintptr_t, uintptr_t, uintptr_t);

namespace fs = std::filesystem;

static bool exists(const std::string &utf8)
{
	std::error_code ec;
	return fs::exists(fs::u8path(utf8), ec);
}

static void writeText(const std::string &utf8, const std::string &text)
{
	std::ofstream out(fs::u8path(utf8), std::ios::binary);
	out << text;
	assert(out.good());
}

static uintptr_t store(const std::string &name, const std::string &bytes)
{
	CacheStoreArgs a{ (uint64_t)(uintptr_t)name.data(), name.size(), (uint64_t)(uintptr_t)bytes.data(), bytes.size() };
	return ce_cache_dispatch(CACHE_OP_STORE, (uintptr_t)&a, 0, 0, 0, 0);
}

static std::string fetch(const std::string &name)
{
	CacheFetchArgs a{ (uint64_t)(uintptr_t)name.data(), name.size(), 0, 0 };
	const uintptr_t size = ce_cache_dispatch(CACHE_OP_FETCH, (uintptr_t)&a, 0, 0, 0, 0);
	if (size == 0) return {};
	std::string out(size, '\0');
	a.dst = (uint64_t)(uintptr_t)&out[0];
	a.cap = size;
	return ce_cache_dispatch(CACHE_OP_FETCH, (uintptr_t)&a, 0, 0, 0, 0) == size ? out : std::string();
}

int main()
{
	std::error_code ec;
	/* "chimera-" e-acute u-umlaut "-test", in UTF-8 */
	const fs::path base = fs::temp_directory_path(ec) / fs::u8path("chimera-\xc3\xa9\xc3\xbc-test");
	fs::remove_all(base, ec);
	fs::create_directories(base, ec);
	assert(!ec);
	const std::string dir = base.u8string();

	// ---- the input journal: written, read back, removed ----
	{
		const std::string path = dir + "/work.journal";
		ce_movie_log *log = ce_movie_log_new();
		const char *text = "[Input]\nLogKey:#A|\n|.|\n|A|\n[/Input]\n";
		assert(ce_movie_log_parse(log, text, std::strlen(text)) == 0);
		assert(ce_movie_log_journal_open(log, path.c_str()) == 0);
		assert(exists(path));
		// opening again rewrites the journal beside itself and moves it over
		assert(ce_movie_log_journal_open(log, path.c_str()) == 0);
		ce_movie_log *back = ce_movie_log_new();
		assert(ce_movie_log_journal_replay(back, path.c_str()) > 0);
		assert(ce_movie_log_count(back) == 2);
		ce_movie_log_free(back);
		ce_movie_log_journal_close(log, 1);
		assert(!exists(path) && !exists(path + ".new"));
		ce_movie_log_free(log);
	}

	// ---- the compile cache: kept, read back, replaced ----
	{
		const std::string cache = dir + "/Cache/PrecompiledCode/0123";
		ce_cache_dir(cache.c_str());
		assert(exists(cache));
		const std::string name = "ppu/m\xc3\xb3" "dulo.obj";
		assert(store(name, "compiled") == 1);
		assert(fetch(name) == "compiled");
		assert(store(name, "again") == 1);
		assert(fetch(name) == "again");

		// Names that would reach outside the cache, or a device, are refused.
		// On Windows a backslash separates folders, so the first two would
		// climb out; everywhere the rule is one rule.
		for (const char *bad : { "..\\..\\escaped", "a\\..\\..\\escaped", "../escaped", "/escaped", "a/../b",
				 "c:escaped", "a//b", "a/./b", "trailing.", "trailing ", "CON", "x/nul.txt", "x/COM1.obj", "lpt9" })
		{
			assert(store(bad, "x") == 0);
		}
		assert(!exists(dir + "/Cache/escaped") && !exists(dir + "/Cache/PrecompiledCode/escaped"));
		ce_cache_dir(nullptr);
	}

	// ---- the media maker: a folder with an accent, packed into a file with one ----
	{
		const std::string in = dir + "/disc \xc3\xa9";
		fs::create_directories(fs::u8path(in + "/sub"), ec);
		writeText(in + "/sub/readme.txt", "contents");
		const std::string out = dir + "/imagen \xc3\xa9.zip";
		if (ce_media_make(in.c_str(), out.c_str(), CE_MEDIA_ZIP_STORED, nullptr, nullptr) != 1)
		{
			std::fprintf(stderr, "media: %s\n", ce_media_last_error());
			assert(false);
		}
		assert(exists(out));
	}

	// ---- a folder inside that cannot be read: an error, not an exception ----
	// (Linux only: a folder Windows will not list needs its access rules changed)
#ifndef _WIN32
	if (geteuid() != 0)
	{
		const std::string in = dir + "/locked";
		fs::create_directories(fs::u8path(in + "/closed"), ec);
		writeText(in + "/open.txt", "x");
		fs::permissions(fs::u8path(in + "/closed"), fs::perms::none, ec);
		const int32_t made = ce_media_make(in.c_str(), (dir + "/locked.zip").c_str(), CE_MEDIA_ZIP_STORED, nullptr, nullptr);
		fs::permissions(fs::u8path(in + "/closed"), fs::perms::owner_all, ec);
		assert(made == 0);
		assert(std::string(ce_media_last_error()).find("cannot read") != std::string::npos);
	}
#endif

	// ---- a project saved, saved again over itself, and opened ----
	{
		const std::string path = dir + "/proyecto \xc3\xa9.chimeraProject";
		const char *err = nullptr;
		ce_project *p = ce_project_new();
		ce_project_set_title(p, "first");
		assert(ce_project_save(p, path.c_str(), &err) == 0);
		ce_project_set_title(p, "second");
		assert(ce_project_save(p, path.c_str(), &err) == 0);
		ce_project_free(p);
		assert(!exists(path + ".writing"));
		p = ce_project_open(path.c_str(), &err);
		assert(p != nullptr);
		assert(std::string(ce_project_title(p)) == "second");
		ce_project_free(p);
	}

	fs::remove_all(base, ec);
	std::printf("utf8_paths: all assertions passed\n");
	return 0;
}
