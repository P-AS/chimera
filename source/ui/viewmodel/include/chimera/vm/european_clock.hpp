#pragma once
/* The clock Chimera writes a core version's date in (chimera issue 331,
 * user-decided 2026-10-20): Central European time as Zurich keeps it - CET
 * (UTC+1), and CEST (UTC+2) from 01:00 UTC on the last Sunday of March to
 * 01:00 UTC on the last Sunday of October - with the zone after the time,
 * "2026-10-18 18:35 CEST". Worked out here rather than asked of the
 * system's time zones, so every computer writes the same text for one
 * moment, as Chimera does. */

#include <cstdint>
#include <string>

namespace chimera::vm {

/* "2026-10-21T16:11:51Z", with seconds or not, a fraction or not, and "Z",
 * "+02:00" or no zone (read as UTC), into seconds since 1970 in UTC */
bool parseIsoTime(const std::string &text, int64_t &utcSeconds);

/* a moment, as "2026-10-18 18:35 CEST" */
std::string europeanTime(int64_t utcSeconds);

} // namespace chimera::vm
