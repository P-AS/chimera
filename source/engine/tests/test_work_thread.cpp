/* test_work_thread.cpp - a helper thread outlives a job that throws.
 *
 * An exception that leaves a std::thread's function ends the process
 * (std::terminate). The greenzone's helpers run jobs that allocate a great
 * deal - packing a PS3 anchor wants about a gigabyte - so one that runs out
 * of memory must cost that job, not the session.
 */

#include "../source/work_thread.hpp"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <new>
#include <stdexcept>

int main()
{
	chimera::WorkThread helper("test");
	if (!helper.threaded())
	{
		std::printf("work_thread: skipped (helpers are off: CHIMERA_HELPERS=0)\n");
		return 0;
	}
	std::atomic<int> ran{ 0 };
	helper.post([] { throw std::bad_alloc(); });
	helper.post([&] { ran++; });
	helper.drain();
	assert(ran == 1);

	helper.post([] { throw std::runtime_error("not an allocation"); });
	helper.post([] { throw 7; });
	helper.post([&] { ran++; });
	helper.drain();
	assert(ran == 2);
	assert(helper.pending() == 0);
	helper.stop();

	std::printf("work_thread: all assertions passed\n");
	return 0;
}
