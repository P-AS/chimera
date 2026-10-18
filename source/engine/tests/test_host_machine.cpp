/* test_host_machine.cpp - what the computer says of itself for a bug report.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`. Each figure is held
 * to what Linux says of the same thing another way, so a reading that is
 * merely not empty does not pass.
 */

#include "chimera/engine.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

int main()
{
	const std::string name = ce_host_cpu_name();
	std::printf("processor: \"%s\"\n", name.c_str());
#if defined(__x86_64__) || defined(__i386__)
	assert(!name.empty());
	assert(name.front() != ' ' && name.back() != ' ');
	for (char c : name) assert(c >= 0x20 && c < 0x7f);
#endif

	const uint64_t total = ce_host_memory_total();
	std::printf("memory: %llu bytes\n", static_cast<unsigned long long>(total));
	assert(total > (256ull << 20));
	/* what can be handed out is part of what is installed */
	assert(ce_compile_memory_to_spare() <= total);

#ifdef __linux__
	/* the kernel's own words: "model name : <the brand string>" */
	if (FILE *f = std::fopen("/proc/cpuinfo", "r"))
	{
		char line[512];
		while (std::fgets(line, sizeof line, f) != nullptr)
		{
			if (std::strncmp(line, "model name", 10) != 0) continue;
			std::string kernel = std::strchr(line, ':') + 1;
			while (!kernel.empty() && (kernel.back() == '\n' || kernel.back() == ' ')) kernel.pop_back();
			while (!kernel.empty() && kernel.front() == ' ') kernel.erase(0, 1);
			std::printf("the kernel's: \"%s\"\n", kernel.c_str());
			assert(kernel == name);
			break;
		}
		std::fclose(f);
	}
#endif
	std::printf("host machine: ok\n");
	return 0;
}
