/* test_kept_pictures.cpp - the pictures of frames already drawn (chimera#190).
 *
 * A picture kept comes back bit for bit and with its size; the least recently
 * used go first when the budget is full; what was drawn past an edit is
 * forgotten; and switched off it keeps nothing.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/kept_pictures.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace
{
std::vector<uint32_t> picture(uint32_t seed, int32_t w, int32_t h, bool noisy)
{
	std::vector<uint32_t> p((size_t)w * (size_t)h);
	std::mt19937 rng(seed);
	for (size_t i = 0; i < p.size(); i++) p[i] = noisy ? (uint32_t)rng() : 0xFF000000u | (seed * 97u + (uint32_t)(i / 64));
	return p;
}

/* Keeps a picture and waits for it, the way a test wants it: a frame of a
 * game is followed by a sixtieth of a second, a line of a test by nothing,
 * and a helper three pictures behind is told to keep no more. */
bool keepNow(CeKeptPictures &kept, int64_t frame, const std::vector<uint32_t> &p, int32_t w, int32_t h)
{
	return kept.keep(frame, p.data(), w, h) && kept.has(frame);
}

void aPictureComesBackWhole()
{
	CeKeptPictures kept;
	const auto a = picture(1, 320, 240, false), b = picture(2, 640, 480, true);
	assert(!kept.keep(5, a.data(), 320, 240) && kept.count() == 0); /* off until given a budget */
	kept.budget(64u << 20);
	assert(keepNow(kept, 5, a, 320, 240) && keepNow(kept, 9, b, 640, 480));
	assert(kept.count() == 2 && kept.has(5) && kept.has(9) && !kept.has(6));
	assert(kept.bytes() > 0 && kept.bytes() < (a.size() + b.size()) * 4 + 4096);

	std::vector<uint32_t> out(640 * 480, 0x12345678u);
	int32_t w = 0, h = 0;
	assert(kept.show(5, out.data(), out.size(), &w, &h) && w == 320 && h == 240);
	assert(std::memcmp(out.data(), a.data(), a.size() * 4) == 0 && out[a.size()] == 0x12345678u);
	assert(kept.show(9, out.data(), out.size(), &w, &h) && w == 640 && h == 480);
	assert(std::memcmp(out.data(), b.data(), b.size() * 4) == 0);
	assert(!kept.show(6, out.data(), out.size(), &w, &h));
	/* a buffer too small for it gets nothing, and the picture stays */
	assert(!kept.show(9, out.data(), 100, &w, &h) && kept.has(9));

	/* kept again, a frame has the new picture */
	assert(keepNow(kept, 5, b, 640, 480) && kept.count() == 2);
	assert(kept.show(5, out.data(), out.size(), &w, &h) && w == 640 && std::memcmp(out.data(), b.data(), b.size() * 4) == 0);

	kept.budget(0);
	assert(kept.count() == 0 && kept.bytes() == 0 && !kept.keep(5, a.data(), 320, 240));
}

void theLeastRecentlyUsedGoFirst()
{
	CeKeptPictures kept;
	/* noise does not compress: each picture is about its own size, 64 KiB */
	const int32_t w = 128, h = 128;
	kept.budget(5 * 70000);
	for (int64_t f = 0; f < 5; f++)
	{
		const auto p = picture((uint32_t)f + 10, w, h, true);
		assert(keepNow(kept, f, p, w, h));
	}
	assert(kept.count() == 5);
	std::vector<uint32_t> out((size_t)w * h);
	int32_t gw, gh;
	assert(kept.show(0, out.data(), out.size(), &gw, &gh)); /* 0 was the oldest; now it is the newest */
	const auto more = picture(99, w, h, true);
	assert(keepNow(kept, 7, more, w, h) && keepNow(kept, 8, more, w, h));
	assert(kept.bytes() <= kept.budget());
	assert(kept.has(0) && !kept.has(1) && !kept.has(2) && kept.has(3) && kept.has(4) && kept.has(7) && kept.has(8));

	/* one larger than the whole budget is not kept, and costs nothing that was */
	const auto huge = picture(5, 512, 512, true);
	const int64_t before = kept.count();
	kept.keep(20, huge.data(), 512, 512);
	assert(kept.count() == before && !kept.has(20));
	/* and one too large to copy in a frame's time is refused outright */
	std::vector<uint32_t> vast(CeKeptPictures::MaxRawBytes / 4 + 16);
	assert(!kept.keep(21, vast.data(), (int32_t)vast.size() / 16, 16) && !kept.has(21));
}

void whatWasDrawnPastAnEditIsForgotten()
{
	CeKeptPictures kept;
	kept.budget(16u << 20);
	const auto p = picture(3, 64, 64, false);
	for (int64_t f : { 10, 11, 12, 30, 31 }) assert(keepNow(kept, f, p, 64, 64));
	kept.dropAfter(12);
	assert(kept.count() == 3 && kept.has(12) && !kept.has(30) && !kept.has(31));
	const uint64_t left = kept.bytes();
	kept.dropAfter(100);
	assert(kept.count() == 3 && kept.bytes() == left);
	kept.drop(11);
	kept.drop(11);
	assert(kept.count() == 2 && !kept.has(11));
	kept.dropAfter(-1);
	assert(kept.count() == 0 && kept.bytes() == 0);
}

/* What keeping a picture costs the frame it is kept on: printed, not held to
 * a number - the machine running the test is not the one running a game. The
 * picture is a game's kind: smooth areas, edges, some noise. */
/* The helper is never more than three pictures behind: the frame that would
 * make it four keeps none, and nothing is lost but that picture. Forgetting
 * waits for what is being packed, so a picture posted before an edit cannot
 * come back after it. */
void aHelperThatIsBehindKeepsNoMore()
{
	CeKeptPictures kept;
	kept.budget(512u << 20);
	const auto big = picture(8, 1600, 1200, true);
	int taken = 0;
	for (int64_t f = 0; f < 40; f++) taken += kept.keep(f, big.data(), 1600, 1200) ? 1 : 0;
	assert(taken >= 1 && taken <= 40);
	assert(kept.count() == taken); /* every one taken in is there, once waited for */
	kept.keep(100, big.data(), 1600, 1200);
	kept.keep(101, big.data(), 1600, 1200);
	kept.dropAfter(50); /* while they are being packed */
	assert(!kept.has(100) && !kept.has(101) && kept.count() == taken);
	std::printf("a helper kept %d of 40 pictures posted with no time between them\n", taken);
}

void whatAFrameCosts()
{
	for (const auto &size : { std::pair<int32_t, int32_t>{ 640, 480 }, { 1280, 720 }, { 1920, 1080 } })
	{
		const int32_t w = size.first, h = size.second;
		std::vector<uint32_t> p((size_t)w * (size_t)h);
		std::mt19937 rng(7);
		for (int32_t y = 0; y < h; y++)
			for (int32_t x = 0; x < w; x++)
				p[(size_t)y * w + x] = 0xFF000000u | (uint32_t)((x / 3 + y / 5) & 0xFF) << 16 | (uint32_t)((x ^ y) & 0xF0) << 8
					| (uint32_t)((x * y / 97) & 0xFF) | ((rng() & 7) == 0 ? rng() & 0x0F0F0F : 0);
		CeKeptPictures kept;
		kept.budget(256u << 20);
		const auto t0 = std::chrono::steady_clock::now();
		/* one at a time, as frames come: what the frame pays is the copy, and
		 * the helper's part is waited for apart */
		double paid = 0;
		const auto t0b = std::chrono::steady_clock::now();
		for (int64_t f = 0; f < 10; f++)
		{
			p[(size_t)f] ^= 1;
			const auto k0 = std::chrono::steady_clock::now();
			assert(kept.keep(f, p.data(), w, h));
			paid += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - k0).count();
			assert(kept.has(f));
		}
		(void)t0b;
		const auto t1 = std::chrono::steady_clock::now();
		std::vector<uint32_t> out(p.size());
		int32_t gw, gh;
		for (int64_t f = 0; f < 10; f++) assert(kept.show(f, out.data(), out.size(), &gw, &gh));
		const auto t2 = std::chrono::steady_clock::now();
		const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count() / 10; };
		std::printf("%dx%d: a frame pays %.2f ms to keep its picture (packing takes %.1f ms on the helper), %.1f ms to show one, %llu KiB each\n",
			w, h, paid / 10, ms(t0, t1), ms(t1, t2), (unsigned long long)(kept.bytes() / 10 / 1024));
	}
}
} // namespace

int main()
{
	aPictureComesBackWhole();
	theLeastRecentlyUsedGoFirst();
	whatWasDrawnPastAnEditIsForgotten();
	aHelperThatIsBehindKeepsNoMore();
	whatAFrameCosts();
	std::printf("test_kept_pictures: ok\n");
	return 0;
}
