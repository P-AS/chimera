/* test_average_fps.cpp - the average speed counts free running and nothing else.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/average_fps.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

int main()
{
	const int64_t frame60 = 1000000000 / 60; /* a frame at 60 a second, in nanoseconds */

	/* Nothing counted yet: zero, not a division by nothing. */
	{
		CeAverageFps a;
		assert(a.fps() == 0.0);
	}

	/* Six hundred frames at 60 a second, then six hundred at 30: 40 on average
	 * (1200 frames in 30 seconds), where a moving figure would say 30. */
	{
		CeAverageFps a;
		for (int i = 0; i < 600; i++) a.pass(1, frame60, false, false, false);
		assert(std::fabs(a.fps() - 60.0) < 0.001);
		for (int i = 0; i < 600; i++) a.pass(1, 2 * frame60, false, false, false);
		assert(std::fabs(a.fps() - 40.0) < 0.001);
	}

	/* A pass that emulated nothing still took its time: the machine was
	 * running, and slowly. */
	{
		CeAverageFps a;
		a.pass(1, frame60, false, false, false);
		a.pass(0, frame60, false, false, false);
		assert(std::fabs(a.fps() - 30.0) < 0.001);
	}

	/* An hour on pause, frames advanced by hand and a rewind change nothing:
	 * neither their frames nor their time. */
	{
		CeAverageFps a;
		for (int i = 0; i < 60; i++) a.pass(1, frame60, false, false, false);
		const double before = a.fps();
		a.pass(0, 3600ll * 1000000000ll, true, false, false);   /* paused */
		for (int i = 0; i < 10; i++) a.pass(1, 500000000, false, true, false); /* stepping, two a second */
		for (int i = 0; i < 100; i++) a.pass(1, frame60 / 4, false, false, true); /* going back */
		assert(near(a.fps(), before));
		assert(a.frames == 60);
	}

	/* A reset starts again from nothing. */
	{
		CeAverageFps a;
		for (int i = 0; i < 60; i++) a.pass(1, frame60, false, false, false);
		a.reset();
		assert(a.fps() == 0.0 && a.frames == 0 && a.nanoseconds == 0);
		for (int i = 0; i < 60; i++) a.pass(1, 2 * frame60, false, false, false);
		assert(std::fabs(a.fps() - 30.0) < 0.001);
	}

	/* Nonsense in, nothing counted: a clock that went backwards, a negative count. */
	{
		CeAverageFps a;
		a.pass(1, 0, false, false, false);
		a.pass(1, -5, false, false, false);
		a.pass(-1, frame60, false, false, false);
		assert(a.fps() == 0.0);
	}

	std::printf("average_fps: ok\n");
	return 0;
}
