/* Chimera's bizstring library (Lua). A Lua string is UTF-8; the library sees
 * it as UTF-16, so positions, lengths and widths are counted in UTF-16 units,
 * and what is not UTF-8 reads as U+FFFD. encode and decode go through the
 * system's own code page converters (Windows' on Windows, iconv elsewhere),
 * by the encodings' usual names ("utf-8", "shift_jis", "windows-1252"). One C function, __chimera_string(operation, ...), and the
 * library itself in lua_libraries.cpp. */

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <iconv.h>
#endif

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace chimera::vm {

namespace {
using U16 = std::u16string;

U16 toUtf16(const std::string &s)
{
	U16 out;
	out.reserve(s.size());
	const auto *b = reinterpret_cast<const unsigned char *>(s.data());
	const size_t n = s.size();
	for (size_t i = 0; i < n;)
	{
		const unsigned c = b[i];
		uint32_t cp = 0xFFFD;
		size_t len = 1;
		auto cont = [&](size_t k) { return i + k < n && (b[i + k] & 0xC0) == 0x80; };
		if (c < 0x80)
			cp = c;
		else if (c >= 0xC2 && c < 0xE0 && cont(1))
			cp = (c & 0x1F) << 6 | (b[i + 1] & 0x3F), len = 2;
		else if (c >= 0xE0 && c < 0xF0 && cont(1) && cont(2))
		{
			cp = (c & 0x0F) << 12 | (b[i + 1] & 0x3F) << 6 | (b[i + 2] & 0x3F);
			len = 3;
			if (cp < 0x800 || (cp >= 0xD800 && cp < 0xE000)) cp = 0xFFFD;
		}
		else if (c >= 0xF0 && c < 0xF5 && cont(1) && cont(2) && cont(3))
		{
			cp = (c & 0x07) << 18 | (b[i + 1] & 0x3F) << 12 | (b[i + 2] & 0x3F) << 6 | (b[i + 3] & 0x3F);
			len = 4;
			if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD;
		}
		i += len;
		if (cp >= 0x10000)
		{
			cp -= 0x10000;
			out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
			out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
		}
		else
			out.push_back(static_cast<char16_t>(cp));
	}
	return out;
}

std::string toUtf8(const U16 &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++)
	{
		uint32_t cp = s[i];
		if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
			cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
		else if (cp >= 0xD800 && cp < 0xE000)
			cp = 0xFFFD; /* half a pair */
		if (cp < 0x80)
			out += static_cast<char>(cp);
		else if (cp < 0x800)
			out += static_cast<char>(0xC0 | cp >> 6), out += static_cast<char>(0x80 | (cp & 0x3F));
		else if (cp < 0x10000)
			out += static_cast<char>(0xE0 | cp >> 12), out += static_cast<char>(0x80 | (cp >> 6 & 0x3F)),
				out += static_cast<char>(0x80 | (cp & 0x3F));
		else
			out += static_cast<char>(0xF0 | cp >> 18), out += static_cast<char>(0x80 | (cp >> 12 & 0x3F)),
				out += static_cast<char>(0x80 | (cp >> 6 & 0x3F)), out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	return out;
}

/* Unicode's white space, in UTF-16 */
bool isSpace(char16_t c)
{
	return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
		c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

/* Case, the same in every language, for the letters scripts meet:
 * Latin (with its first extensions), Greek, Cyrillic and the full-width
 * Latin of Japanese text */
char16_t upper(char16_t c)
{
	if (c >= u'a' && c <= u'z') return c - 32;
	if (c < 0xB5) return c;
	if (c == 0xB5) return 0x39C;
	if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
	if (c == 0xFF) return 0x178;
	if ((c >= 0x100 && c <= 0x12F) || (c >= 0x132 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return (c & 1) ? c - 1 : c;
	if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c : c - 1;
	if (c == 0x131) return u'I';
	if (c == 0x17F) return u'S';
	if (c == 0x3C2) return 0x3A3;
	if (c >= 0x3B1 && c <= 0x3C9) return c - 32;
	if (c >= 0x430 && c <= 0x44F) return c - 32;
	if (c >= 0x450 && c <= 0x45F) return c - 80;
	if (c >= 0xFF41 && c <= 0xFF5A) return c - 32;
	return c;
}

char16_t lower(char16_t c)
{
	if (c >= u'A' && c <= u'Z') return c + 32;
	if (c < 0xC0) return c;
	if (c <= 0xDE && c != 0xD7) return c + 32;
	if (c == 0x178) return 0xFF;
	if ((c >= 0x100 && c <= 0x12F) || (c >= 0x132 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return (c & 1) ? c : c + 1;
	if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c + 1 : c;
	if (c == 0x130) return u'i';
	if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
	if (c >= 0x410 && c <= 0x42F) return c + 32;
	if (c >= 0x400 && c <= 0x40F) return c + 80;
	if (c >= 0xFF21 && c <= 0xFF3A) return c + 32;
	return c;
}

std::string lowered(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

/* UTF-16 and UTF-32 are done here on every system, with no byte order mark */
bool unicodeForm(const std::string &name, int &bytes, bool &big)
{
	const std::string n = lowered(name);
	if (n == "utf-16" || n == "unicode" || n == "utf-16le") return bytes = 2, big = false, true;
	if (n == "utf-16be" || n == "unicodefffe") return bytes = 2, big = true, true;
	if (n == "utf-32" || n == "utf-32le") return bytes = 4, big = false, true;
	if (n == "utf-32be") return bytes = 4, big = true, true;
	return false;
}

U16 fromUnicodeForm(const std::string &bytes, int width, bool big)
{
	U16 out;
	for (size_t i = 0; i + static_cast<size_t>(width) <= bytes.size(); i += static_cast<size_t>(width))
	{
		uint32_t v = 0;
		for (int k = 0; k < width; k++)
		{
			const uint32_t b = static_cast<unsigned char>(bytes[i + static_cast<size_t>(k)]);
			v |= big ? b << (8 * (width - 1 - k)) : b << (8 * k);
		}
		if (width == 2)
			out.push_back(static_cast<char16_t>(v));
		else if (v >= 0x10000 && v <= 0x10FFFF)
			out.push_back(static_cast<char16_t>(0xD800 + ((v - 0x10000) >> 10))), out.push_back(static_cast<char16_t>(0xDC00 + ((v - 0x10000) & 0x3FF)));
		else
			out.push_back(static_cast<char16_t>(v > 0xFFFF || (v >= 0xD800 && v < 0xE000) ? 0xFFFD : v));
	}
	/* a byte left over cannot be read */
	if (bytes.size() % static_cast<size_t>(width) != 0) out.push_back(0xFFFD);
	return out;
}

std::string toUnicodeForm(const U16 &s, int width, bool big)
{
	std::string out;
	auto put = [&](uint32_t v) {
		for (int k = 0; k < width; k++) out += static_cast<char>(big ? v >> (8 * (width - 1 - k)) : v >> (8 * k));
	};
	for (size_t i = 0; i < s.size(); i++)
	{
		uint32_t cp = s[i];
		if (width == 4 && cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
			cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
		put(cp);
	}
	return out;
}

#ifdef _WIN32
/* an encoding's name, as Windows' code page */
UINT codePage(const std::string &name)
{
	static const struct
	{
		const char *name;
		UINT page;
	} pages[] = {{"utf-8", 65001}, {"us-ascii", 20127}, {"ascii", 20127}, {"iso-8859-1", 28591}, {"latin1", 28591},
		{"shift_jis", 932}, {"shift-jis", 932}, {"sjis", 932}, {"x-sjis", 932}, {"euc-jp", 20932}, {"iso-2022-jp", 50220},
		{"gb2312", 936}, {"gbk", 936}, {"big5", 950}, {"ks_c_5601-1987", 949}, {"euc-kr", 51949}, {"koi8-r", 20866},
		{"koi8-u", 21866}, {"utf-7", 65000}};
	const std::string n = lowered(name);
	for (const auto &p : pages)
		if (n == p.name) return p.page;
	auto number = [&](const char *prefix) -> UINT {
		const size_t len = std::strlen(prefix);
		if (n.compare(0, len, prefix) != 0 || n.size() == len || n.find_first_not_of("0123456789", len) != std::string::npos) return 0;
		return static_cast<UINT>(std::stoul(n.substr(len)));
	};
	if (UINT p = number("windows-")) return p;
	if (UINT p = number("cp")) return p;
	if (UINT p = number("ibm")) return p;
	if (UINT p = number("iso-8859-")) return 28590 + p;
	return 0;
}
#else
/* an encoding's name, as iconv knows it: where the name is a Windows code
 * page under another name, that code page */
std::string iconvName(const std::string &name)
{
	const std::string n = lowered(name);
	if (n == "shift_jis" || n == "shift-jis" || n == "sjis" || n == "x-sjis") return "CP932";
	if (n == "gb2312" || n == "gbk") return "CP936";
	if (n == "big5") return "CP950";
	if (n == "ks_c_5601-1987") return "CP949";
	if (n == "us-ascii") return "ASCII";
	if (n.rfind("windows-", 0) == 0) return "CP" + n.substr(8);
	return n;
}
#endif

/* bytes in an encoding to UTF-16; false when the encoding is not known */
bool decodeBytes(const std::string &bytes, const std::string &encoding, U16 &out)
{
	int width = 0;
	bool big = false;
	if (lowered(encoding) == "utf-8")
	{
		out = toUtf16(bytes);
		return true;
	}
	if (unicodeForm(encoding, width, big))
	{
		out = fromUnicodeForm(bytes, width, big);
		return true;
	}
#ifdef _WIN32
	const UINT page = codePage(encoding);
	if (page == 0 || !IsValidCodePage(page)) return false;
	if (bytes.empty()) return out.clear(), true;
	const int n = MultiByteToWideChar(page, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
	if (n <= 0) return false;
	std::wstring w(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(page, 0, bytes.data(), static_cast<int>(bytes.size()), w.data(), n);
	out.assign(w.begin(), w.end());
	return true;
#else
	iconv_t cd = iconv_open("UTF-16LE", iconvName(encoding).c_str());
	if (cd == reinterpret_cast<iconv_t>(-1)) return false;
	out.clear();
	std::string in = bytes;
	char *src = in.data();
	size_t left = in.size();
	char buf[1024];
	while (left > 0)
	{
		char *dst = buf;
		size_t room = sizeof buf;
		const size_t r = iconv(cd, &src, &left, &dst, &room);
		for (char *p = buf; p + 1 < dst; p += 2)
			out.push_back(static_cast<char16_t>(static_cast<unsigned char>(p[0]) | static_cast<unsigned char>(p[1]) << 8));
		if (r == static_cast<size_t>(-1) && errno != E2BIG)
		{
			/* a byte that cannot be read: U+FFFD for it, and on */
			out.push_back(0xFFFD);
			src++;
			left--;
		}
	}
	iconv_close(cd);
	return true;
#endif
}

/* UTF-16 to bytes in an encoding; false when the encoding is not known */
bool encodeText(const U16 &text, const std::string &encoding, std::string &out)
{
	int width = 0;
	bool big = false;
	if (lowered(encoding) == "utf-8")
	{
		out = toUtf8(text);
		return true;
	}
	if (unicodeForm(encoding, width, big))
	{
		out = toUnicodeForm(text, width, big);
		return true;
	}
#ifdef _WIN32
	const UINT page = codePage(encoding);
	if (page == 0 || !IsValidCodePage(page)) return false;
	if (text.empty()) return out.clear(), true;
	const std::wstring w(text.begin(), text.end());
	const int n = WideCharToMultiByte(page, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	if (n <= 0) return false;
	out.assign(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(page, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
	return true;
#else
	iconv_t cd = iconv_open(iconvName(encoding).c_str(), "UTF-16LE");
	if (cd == reinterpret_cast<iconv_t>(-1)) return false;
	out.clear();
	std::string in;
	for (char16_t c : text) in += static_cast<char>(c & 0xFF), in += static_cast<char>(c >> 8);
	char *src = in.data();
	size_t left = in.size();
	char buf[1024];
	while (left > 0)
	{
		char *dst = buf;
		size_t room = sizeof buf;
		const size_t r = iconv(cd, &src, &left, &dst, &room);
		out.append(buf, static_cast<size_t>(dst - buf));
		if (r == static_cast<size_t>(-1) && errno != E2BIG)
		{
			/* a character the encoding has not: '?' */
			out += '?';
			src += 2;
			left -= std::min<size_t>(left, 2);
		}
	}
	iconv_close(cd);
	return true;
#endif
}

U16 arg16(lua_State *L, int at) { return toUtf16(luaL_checkstring(L, at)); }

void push16(lua_State *L, const U16 &s)
{
	const std::string t = toUtf8(s);
	lua_pushlstring(L, t.data(), t.size());
}

/* a string given, or nil and empty: Chimera's library answers nil for those */
bool blank(lua_State *L, int at) { return lua_isnoneornil(L, at) || (lua_type(L, at) == LUA_TSTRING && lua_rawlen(L, at) == 0); }

int64_t whole(lua_State *L, int at)
{
	if (lua_isinteger(L, at)) return lua_tointeger(L, at);
	return static_cast<int64_t>(luaL_checknumber(L, at));
}
} // namespace

/* __chimera_string(operation, ...): bizstring's work */
int luaChimeraString(lua_State *L)
{
	const std::string op = luaL_checkstring(L, 1);
	if (op == "decode")
	{
		luaL_checktype(L, 2, LUA_TTABLE);
		const std::string encoding = luaL_optstring(L, 3, "utf-8");
		/* the values of table[1], table[2]... up to the first absent one, as
		 * Chimera reads a byte table - from table[0] when there is one, as
		 * memory.readbyterange's tables start there */
		std::string bytes;
		lua_rawgeti(L, 2, 0);
		const lua_Integer first = lua_isnil(L, -1) ? 1 : 0;
		lua_pop(L, 1);
		for (lua_Integer i = first;; i++)
		{
			if (lua_rawgeti(L, 2, i) == LUA_TNIL)
			{
				lua_pop(L, 1);
				break;
			}
			bytes += static_cast<char>(static_cast<unsigned char>(lua_tointeger(L, -1)));
			lua_pop(L, 1);
		}
		U16 text;
		if (!decodeBytes(bytes, encoding, text)) return luaL_error(L, "'%s' is not a supported encoding name", encoding.c_str());
		push16(L, text);
		return 1;
	}
	if (op == "encode")
	{
		const U16 text = arg16(L, 2);
		const std::string encoding = luaL_optstring(L, 3, "utf-8");
		std::string bytes;
		if (!encodeText(text, encoding, bytes)) return luaL_error(L, "'%s' is not a supported encoding name", encoding.c_str());
		lua_createtable(L, static_cast<int>(bytes.size()), 0);
		for (size_t i = 0; i < bytes.size(); i++)
		{
			lua_pushinteger(L, static_cast<unsigned char>(bytes[i]));
			lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
		}
		return 1;
	}
	if (op == "fold")
	{
		const U16 s = arg16(L, 2);
		const int64_t width = whole(L, 3);
		if (width < 1) return luaL_error(L, "bizstring.fold: the width must be 1 or more");
#ifdef _WIN32
		const U16 sep = lua_isnoneornil(L, 4) ? U16(u"\r\n") : arg16(L, 4);
#else
		const U16 sep = lua_isnoneornil(L, 4) ? U16(u"\n") : arg16(L, 4);
#endif
		U16 out;
		for (size_t at = 0; at < s.size(); at += static_cast<size_t>(width))
		{
			if (at > 0) out += sep;
			out += s.substr(at, static_cast<size_t>(width));
		}
		push16(L, out);
		return 1;
	}
	if (op == "pad_end" || op == "pad_start")
	{
		U16 s = arg16(L, 2);
		const int64_t length = whole(L, 3);
		const U16 pad = arg16(L, 4);
		if (length < 0) return luaL_error(L, "bizstring.%s: the length must not be negative", op.c_str());
		const char16_t c = pad.empty() ? u' ' : pad[0];
		if (static_cast<int64_t>(s.size()) < length)
		{
			const U16 fill(static_cast<size_t>(length) - s.size(), c);
			s = op == "pad_end" ? s + fill : fill + s;
		}
		push16(L, s);
		return 1;
	}
	if (op == "trim" || op == "toupper" || op == "tolower")
	{
		if (blank(L, 2)) return lua_pushnil(L), 1;
		U16 s = arg16(L, 2);
		if (op == "trim")
		{
			size_t a = 0, b = s.size();
			while (a < b && isSpace(s[a])) a++;
			while (b > a && isSpace(s[b - 1])) b--;
			s = s.substr(a, b - a);
		}
		else
			for (char16_t &c : s) c = op == "toupper" ? upper(c) : lower(c);
		push16(L, s);
		return 1;
	}
	if (op == "replace")
	{
		if (blank(L, 2)) return lua_pushnil(L), 1;
		const U16 s = arg16(L, 2), from = arg16(L, 3);
		const U16 to = lua_isnoneornil(L, 4) ? U16() : arg16(L, 4);
		if (from.empty()) return luaL_error(L, "bizstring.replace: the string to replace cannot be empty");
		U16 out;
		size_t at = 0;
		for (size_t hit = s.find(from); hit != U16::npos; hit = s.find(from, at))
		{
			out += s.substr(at, hit - at) + to;
			at = hit + from.size();
		}
		out += s.substr(at);
		push16(L, out);
		return 1;
	}
	if (op == "substring" || op == "remove")
	{
		if (blank(L, 2)) return lua_pushnil(L), 1;
		const U16 s = arg16(L, 2);
		const int64_t position = whole(L, 3), count = whole(L, 4);
		if (position < 0 || count < 0 || position + count > static_cast<int64_t>(s.size()))
			return luaL_error(L, "bizstring.%s: the position and %s must lie within the string", op.c_str(), op == "substring" ? "length" : "count");
		const size_t p = static_cast<size_t>(position), n = static_cast<size_t>(count);
		push16(L, op == "substring" ? s.substr(p, n) : s.substr(0, p) + s.substr(p + n));
		return 1;
	}
	if (op == "contains" || op == "startswith" || op == "endswith")
	{
		if (blank(L, 2)) return lua_pushboolean(L, 0), 1;
		const U16 s = arg16(L, 2), part = arg16(L, 3);
		bool yes = false;
		if (op == "contains") yes = s.find(part) != U16::npos;
		else if (op == "startswith") yes = s.compare(0, part.size(), part) == 0;
		else yes = s.size() >= part.size() && s.compare(s.size() - part.size(), part.size(), part) == 0;
		lua_pushboolean(L, yes ? 1 : 0);
		return 1;
	}
	if (op == "split")
	{
		lua_newtable(L);
		if (blank(L, 2)) return 1;
		const U16 s = arg16(L, 2);
		const U16 given = lua_type(L, 3) == LUA_TSTRING ? arg16(L, 3) : U16();
		const char16_t sep = given.size() == 1 ? given[0] : u',';
		lua_Integer n = 0;
		size_t at = 0;
		while (at <= s.size())
		{
			size_t end = s.find(sep, at);
			if (end == U16::npos) end = s.size();
			/* consecutive separators make no empty entries */
			if (end > at)
			{
				push16(L, s.substr(at, end - at));
				lua_rawseti(L, -2, ++n);
			}
			at = end + 1;
		}
		return 1;
	}
	return luaL_error(L, "__chimera_string: no operation %s", op.c_str());
}

} // namespace chimera::vm
