#include "chimera/vm/european_clock.hpp"

#include <cctype>
#include <cstdio>

namespace chimera::vm {

namespace {
/* days since 1970-01-01 of a civil date (Howard Hinnant's algorithm) */
int64_t daysFromCivil(int64_t y, int64_t m, int64_t d)
{
	y -= m <= 2;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const int64_t yoe = y - era * 400;
	const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t z, int64_t &y, int64_t &m, int64_t &d)
{
	z += 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const int64_t doe = z - era * 146097;
	const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const int64_t mp = (5 * doy + 2) / 153;
	d = doy - (153 * mp + 2) / 5 + 1;
	m = mp + (mp < 10 ? 3 : -9);
	y = yoe + era * 400 + (m <= 2);
}

/* 01:00 UTC on the last Sunday of a month, in seconds */
int64_t lastSundayOneUtc(int64_t year, int64_t month)
{
	const int64_t next = month == 12 ? daysFromCivil(year + 1, 1, 1) : daysFromCivil(year, month + 1, 1);
	int64_t last = next - 1;
	/* 1970-01-01 was a Thursday: day 0 is weekday 4 (Sunday 0) */
	const int64_t weekday = ((last % 7) + 7 + 4) % 7;
	last -= weekday;
	return last * 86400 + 3600;
}

bool number(const std::string &s, size_t &at, size_t digits, int64_t &out)
{
	if (at + digits > s.size()) return false;
	out = 0;
	for (size_t i = 0; i < digits; i++)
	{
		if (!std::isdigit(static_cast<unsigned char>(s[at + i]))) return false;
		out = out * 10 + (s[at + i] - '0');
	}
	at += digits;
	return true;
}
} // namespace

bool parseIsoTime(const std::string &s, int64_t &utcSeconds)
{
	size_t at = 0;
	int64_t y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
	auto expect = [&](char c) {
		if (at < s.size() && s[at] == c)
		{
			at++;
			return true;
		}
		return false;
	};
	if (!number(s, at, 4, y) || !expect('-') || !number(s, at, 2, mo) || !expect('-') || !number(s, at, 2, d)) return false;
	if (!(expect('T') || expect(' ')) || !number(s, at, 2, h) || !expect(':') || !number(s, at, 2, mi)) return false;
	if (expect(':') && !number(s, at, 2, sec)) return false;
	if (expect('.'))
		while (at < s.size() && std::isdigit(static_cast<unsigned char>(s[at]))) at++;
	int64_t offset = 0;
	if (at < s.size())
	{
		if (s[at] == 'Z' || s[at] == 'z')
			at++;
		else if (s[at] == '+' || s[at] == '-')
		{
			const int64_t sign = s[at] == '-' ? -1 : 1;
			at++;
			int64_t oh = 0, om = 0;
			if (!number(s, at, 2, oh)) return false;
			expect(':');
			if (!number(s, at, 2, om)) return false;
			offset = sign * (oh * 3600 + om * 60);
		}
	}
	if (at != s.size() || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60) return false;
	utcSeconds = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec - offset;
	return true;
}

std::string europeanTime(int64_t utc)
{
	int64_t y = 0, m = 0, d = 0;
	const int64_t days = utc >= 0 ? utc / 86400 : (utc - 86399) / 86400;
	civilFromDays(days, y, m, d);
	const bool summer = utc >= lastSundayOneUtc(y, 3) && utc < lastSundayOneUtc(y, 10);
	const int64_t local = utc + (summer ? 7200 : 3600);
	const int64_t localDays = local >= 0 ? local / 86400 : (local - 86399) / 86400;
	civilFromDays(localDays, y, m, d);
	const int64_t inDay = local - localDays * 86400;
	char out[48];
	std::snprintf(out, sizeof out, "%04lld-%02lld-%02lld %02lld:%02lld %s", static_cast<long long>(y), static_cast<long long>(m),
		static_cast<long long>(d), static_cast<long long>(inDay / 3600), static_cast<long long>(inDay % 3600 / 60), summer ? "CEST" : "CET");
	return out;
}

} // namespace chimera::vm
