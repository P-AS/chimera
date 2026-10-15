/* test_guest_calls.cpp - every export a core resolves gets a way in.
 *
 * On Windows a call into the guest goes through a small stub per export and
 * argument count (host_dyn.cpp). They came from one fixed pool of 256 that
 * was never refilled, so after a few sessions an export got no stub, which
 * reads exactly like an export the core lacks. Built for Linux with the
 * other tests (where a guest call needs no stub) and for Windows by hand
 * (meson compile -C build/meson-windows test_guest_calls), to be run there
 * with libminiboxhost.dll beside it.
 */

#include "../source/host_dyn.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <set>

int main()
{
	std::set<uintptr_t> stubs;
	for (uintptr_t i = 1; i <= 1000; i++)
	{
		const uintptr_t stub = chimera::bridgeGuestCall(0x10000000 + i * 16, 2);
		if (stub == 0)
		{
			std::fprintf(stderr, "guest_calls: no way in for export %zu\n", static_cast<size_t>(i));
			assert(false);
		}
		stubs.insert(stub);
	}
	assert(stubs.size() == 1000);
	/* the same export and argument count is the same way in */
	assert(chimera::bridgeGuestCall(0x10000000 + 16, 2) == chimera::bridgeGuestCall(0x10000000 + 16, 2));
	std::printf("guest_calls: all assertions passed\n");
	return 0;
}
