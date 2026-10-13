/* average_fps.h - how fast the machine has been running, on average.
 *
 * The frame counter on screen is a moving figure: it answers "how fast right
 * now". Somebody comparing two settings wants the other question - how fast
 * over the whole of a stretch of play - and reading a figure that jumps
 * between 9 and 58 does not answer it (chimera issue 265).
 *
 * The average is frames emulated over the time they took, since the last
 * reset. Only free running counts: a pass of the frontend's loop made while
 * the emulator was paused, while frames were being advanced by hand, or while
 * going back, adds neither its frames nor its time - an hour left on pause
 * must not turn 60 into 3, and stepping a frame at a time is the speed of a
 * finger. Fast-forward and turbo count: they are the machine running freely.
 */
#ifndef CHIMERA_AVERAGE_FPS_H
#define CHIMERA_AVERAGE_FPS_H

#include <cstdint>

struct CeAverageFps
{
	int64_t frames = 0;
	int64_t nanoseconds = 0;

	void reset()
	{
		frames = 0;
		nanoseconds = 0;
	}

	/* One pass of the frontend's loop: the frames it emulated (0 or 1 as a
	 * rule), the time since the pass before, and what the emulator was doing. */
	void pass(int64_t passFrames, int64_t passNanoseconds, bool paused, bool stepping, bool goingBack)
	{
		if (paused || stepping || goingBack) return;
		if (passFrames < 0 || passNanoseconds <= 0) return;
		frames += passFrames;
		nanoseconds += passNanoseconds;
	}

	/* Frames a second; 0 until something has counted. */
	double fps() const
	{
		return nanoseconds > 0 ? static_cast<double>(frames) * 1e9 / static_cast<double>(nanoseconds) : 0.0;
	}
};

#endif
