/* compile_sessions.cpp - what the machine says of its memory, and the C face
 * of compile_sessions.h (which says what is counted and why). */

#include "compile_sessions.h"

#include "chimera/engine.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>
#include <cstring>
#endif

CeHostMemory ceReadHostMemory()
{
	CeHostMemory m;
#ifdef _WIN32
	MEMORYSTATUSEX status;
	status.dwLength = sizeof status;
	if (GlobalMemoryStatusEx(&status))
	{
		m.freePhysical = status.ullAvailPhys;
		/* "page file" in this structure's names means commit: what the system
		 * may still promise, with the page file at its present size */
		m.freeCommit = status.ullAvailPageFile;
	}
#else
	/* Linux promises more than it has, so there is one figure: what the
	 * kernel reckons a new program could have without swapping. */
	if (FILE *f = std::fopen("/proc/meminfo", "r"))
	{
		char line[256];
		while (std::fgets(line, sizeof line, f) != nullptr)
		{
			unsigned long long kb = 0;
			if (std::sscanf(line, "MemAvailable: %llu kB", &kb) == 1)
			{
				m.freePhysical = static_cast<uint64_t>(kb) * 1024;
				break;
			}
		}
		std::fclose(f);
	}
#endif
	return m;
}

extern "C" uint64_t ce_compile_memory_to_spare(void)
{
	return ceMemoryToSpare(ceReadHostMemory());
}

extern "C" int32_t ce_compile_sessions(int32_t processors, uint64_t to_spare)
{
	return ceCompileSessions(processors, to_spare);
}

extern "C" uint64_t ce_compile_session_bytes(void)
{
	return kCompileSessionBytes;
}

extern "C" int32_t ce_compile_memory_ran_short(uint64_t least_to_spare)
{
	return ceCompileMemoryRanShort(least_to_spare) ? 1 : 0;
}
