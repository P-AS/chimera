#pragma once
/* What crosses between a view model and whoever drives it - a window, a Lua
 * script, the MCP server or a test: nothing, a truth, a whole number, a real
 * number or a text. Nothing richer crosses, so every driver can speak it. */

#include <cmath>
#include <cstdint>
#include <string>
#include <variant>

namespace chimera::vm {

using Value = std::variant<std::monostate, bool, int64_t, double, std::string>;

inline Value toValue(bool v) { return v; }
inline Value toValue(int32_t v) { return int64_t{v}; }
inline Value toValue(int64_t v) { return v; }
inline Value toValue(double v) { return v; }
inline Value toValue(const std::string &v) { return v; }

/* false when the value cannot be read as the type asked for */
inline bool fromValue(const Value &v, bool &out)
{
	if (const bool *b = std::get_if<bool>(&v)) { out = *b; return true; }
	if (const int64_t *i = std::get_if<int64_t>(&v)) { out = *i != 0; return true; }
	return false;
}

inline bool fromValue(const Value &v, int64_t &out)
{
	if (const int64_t *i = std::get_if<int64_t>(&v)) { out = *i; return true; }
	if (const double *d = std::get_if<double>(&v))
	{
		/* a script's numbers are often real numbers that happen to be whole */
		if (std::isfinite(*d) && *d == std::floor(*d) && std::fabs(*d) < 9.0e18)
		{
			out = static_cast<int64_t>(*d);
			return true;
		}
		return false;
	}
	if (const bool *b = std::get_if<bool>(&v)) { out = *b ? 1 : 0; return true; }
	return false;
}

inline bool fromValue(const Value &v, int32_t &out)
{
	int64_t wide = 0;
	if (!fromValue(v, wide) || wide < INT32_MIN || wide > INT32_MAX) return false;
	out = static_cast<int32_t>(wide);
	return true;
}

inline bool fromValue(const Value &v, double &out)
{
	if (const double *d = std::get_if<double>(&v)) { out = *d; return true; }
	if (const int64_t *i = std::get_if<int64_t>(&v)) { out = static_cast<double>(*i); return true; }
	return false;
}

inline bool fromValue(const Value &v, std::string &out)
{
	if (const std::string *s = std::get_if<std::string>(&v)) { out = *s; return true; }
	return false;
}

/* for messages: the value as a person would write it */
inline std::string describe(const Value &v)
{
	if (std::holds_alternative<std::monostate>(v)) return "nothing";
	if (const bool *b = std::get_if<bool>(&v)) return *b ? "true" : "false";
	if (const int64_t *i = std::get_if<int64_t>(&v)) return std::to_string(*i);
	if (const double *d = std::get_if<double>(&v)) return std::to_string(*d);
	return "\"" + std::get<std::string>(v) + "\"";
}

} // namespace chimera::vm
