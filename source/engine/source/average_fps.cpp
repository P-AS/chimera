/* average_fps.cpp - the average speed counter's C face (average_fps.h says
 * what it counts and why). */

#include "average_fps.h"

#include "chimera/engine.h"

#include <new>

struct ce_average_fps
{
	CeAverageFps impl;
};

extern "C" ce_average_fps *ce_average_fps_create(void)
{
	return new (std::nothrow) ce_average_fps();
}

extern "C" void ce_average_fps_destroy(ce_average_fps *a) { delete a; }

extern "C" void ce_average_fps_reset(ce_average_fps *a)
{
	if (a != nullptr) a->impl.reset();
}

extern "C" void ce_average_fps_pass(ce_average_fps *a, int64_t frames, int64_t nanoseconds, int32_t paused, int32_t stepping, int32_t going_back)
{
	if (a != nullptr) a->impl.pass(frames, nanoseconds, paused != 0, stepping != 0, going_back != 0);
}

extern "C" double ce_average_fps_value(const ce_average_fps *a)
{
	return a != nullptr ? a->impl.fps() : 0.0;
}
