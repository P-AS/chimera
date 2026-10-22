#include "chimera/vm/bindings.hpp"

#include <cstdint>
#include <utility>

namespace chimera::vm {

namespace {

/* Just enough JSON for a bindings file: objects, arrays, strings, and the
 * rest read and dropped. */
struct Json
{
	enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
	std::string text;
	std::vector<Json> items;
	std::vector<std::pair<std::string, Json>> members;
};

class Reader
{
public:
	explicit Reader(std::string_view s) : s_(s) {}

	bool document(Json &out, std::string &error)
	{
		if (!value(out, 0)) { error = error_; return false; }
		space();
		if (i_ != s_.size()) { error = "text after the end at byte " + std::to_string(i_); return false; }
		return true;
	}

private:
	bool fail(const std::string &what)
	{
		if (error_.empty()) error_ = what + " at byte " + std::to_string(i_);
		return false;
	}

	void space()
	{
		while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) i_++;
	}

	bool literal(std::string_view word)
	{
		if (s_.substr(i_, word.size()) != word) return fail("unknown word");
		i_ += word.size();
		return true;
	}

	static void utf8(std::string &out, uint32_t c)
	{
		if (c < 0x80) out += static_cast<char>(c);
		else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
		else if (c < 0x10000)
		{
			out += static_cast<char>(0xE0 | (c >> 12));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xF0 | (c >> 18));
			out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
	}

	bool hex4(uint32_t &out)
	{
		if (i_ + 4 > s_.size()) return fail("short \\u escape");
		out = 0;
		for (int k = 0; k < 4; k++)
		{
			const char c = s_[i_++];
			out <<= 4;
			if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
			else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
			else return fail("bad \\u escape");
		}
		return true;
	}

	bool string(std::string &out)
	{
		i_++; /* the opening quote */
		while (i_ < s_.size())
		{
			const char c = s_[i_++];
			if (c == '"') return true;
			if (c != '\\') { out += c; continue; }
			if (i_ >= s_.size()) break;
			const char e = s_[i_++];
			switch (e)
			{
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u':
			{
				uint32_t c1 = 0;
				if (!hex4(c1)) return false;
				if (c1 >= 0xD800 && c1 < 0xDC00 && s_.substr(i_, 2) == "\\u")
				{
					i_ += 2;
					uint32_t c2 = 0;
					if (!hex4(c2)) return false;
					c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
				}
				utf8(out, c1);
				break;
			}
			default: return fail("bad escape");
			}
		}
		return fail("unfinished string");
	}

	bool value(Json &out, int depth)
	{
		if (depth > 64) return fail("nested too deep");
		space();
		if (i_ >= s_.size()) return fail("unexpected end");
		const char c = s_[i_];
		if (c == '{')
		{
			out.kind = Json::Kind::Object;
			i_++;
			space();
			if (i_ < s_.size() && s_[i_] == '}') { i_++; return true; }
			for (;;)
			{
				space();
				if (i_ >= s_.size() || s_[i_] != '"') return fail("expected a name");
				std::string name;
				if (!string(name)) return false;
				space();
				if (i_ >= s_.size() || s_[i_] != ':') return fail("expected ':'");
				i_++;
				Json v;
				if (!value(v, depth + 1)) return false;
				out.members.emplace_back(std::move(name), std::move(v));
				space();
				if (i_ < s_.size() && s_[i_] == ',') { i_++; continue; }
				if (i_ < s_.size() && s_[i_] == '}') { i_++; return true; }
				return fail("expected ',' or '}'");
			}
		}
		if (c == '[')
		{
			out.kind = Json::Kind::Array;
			i_++;
			space();
			if (i_ < s_.size() && s_[i_] == ']') { i_++; return true; }
			for (;;)
			{
				Json v;
				if (!value(v, depth + 1)) return false;
				out.items.push_back(std::move(v));
				space();
				if (i_ < s_.size() && s_[i_] == ',') { i_++; continue; }
				if (i_ < s_.size() && s_[i_] == ']') { i_++; return true; }
				return fail("expected ',' or ']'");
			}
		}
		if (c == '"') { out.kind = Json::Kind::String; return string(out.text); }
		if (c == 't') { out.kind = Json::Kind::Bool; return literal("true"); }
		if (c == 'f') { out.kind = Json::Kind::Bool; return literal("false"); }
		if (c == 'n') { out.kind = Json::Kind::Null; return literal("null"); }
		if (c == '-' || (c >= '0' && c <= '9'))
		{
			out.kind = Json::Kind::Number;
			while (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E' || (s_[i_] >= '0' && s_[i_] <= '9')))
				out.text += s_[i_++];
			return true;
		}
		return fail("unexpected character");
	}

	std::string_view s_;
	size_t i_ = 0;
	std::string error_;
};

std::string trim(std::string_view s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
	return std::string(s.substr(a, b - a));
}

} // namespace

bool Bindings::parse(std::string_view json, const std::vector<std::string> &buttonNames, std::string &error)
{
	byKey_.clear();
	Json root;
	if (!Reader(json).document(root, error)) return false;
	if (root.kind != Json::Kind::Object)
	{
		error = "the bindings are not a JSON object";
		return false;
	}

	/* The file has a section per controller a package declares ("AllTrollers"
	 * -> "Genesis Controller" -> "P1 Up": "Up, J1 POV0U"). The machine runs
	 * one of them: take the section that names most of its buttons. */
	const Json *best = nullptr;
	size_t bestCount = 0;
	for (const auto &[group, sections] : root.members)
	{
		if (sections.kind != Json::Kind::Object) continue;
		for (const auto &[controller, section] : sections.members)
		{
			if (section.kind != Json::Kind::Object) continue;
			size_t count = 0;
			for (const auto &[button, keys] : section.members)
				for (const auto &name : buttonNames)
					if (keys.kind == Json::Kind::String && name == button) { count++; break; }
			if (count > bestCount) { best = &section; bestCount = count; }
		}
	}
	if (best == nullptr) return true;

	for (const auto &[button, keys] : best->members)
	{
		if (keys.kind != Json::Kind::String) continue;
		int index = -1;
		for (size_t b = 0; b < buttonNames.size(); b++)
			if (buttonNames[b] == button) { index = static_cast<int>(b); break; }
		if (index < 0) continue;
		std::string_view rest = keys.text;
		while (!rest.empty())
		{
			const size_t comma = rest.find(',');
			const std::string key = trim(rest.substr(0, comma));
			if (!key.empty()) byKey_[key].push_back(index);
			if (comma == std::string_view::npos) break;
			rest.remove_prefix(comma + 1);
		}
	}
	return true;
}

const std::vector<int> &Bindings::buttonsOf(const std::string &key) const
{
	static const std::vector<int> none;
	const auto it = byKey_.find(key);
	return it == byKey_.end() ? none : it->second;
}

} // namespace chimera::vm
