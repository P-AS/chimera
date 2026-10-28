/* language.cpp - Chimera's language tables and the ce_language_/ce_text ABI.
 * See language.hpp and docs/translations.md. */
#include "language.hpp"

#include "../../extern/cjson/cJSON.h"
#include "chimera/engine.h"
#include "file_io.hpp"
#include "host_dyn.hpp"
#include "thread_string.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>

namespace chimera::lang {

namespace {

std::string baseOf(const std::string &code)
{
	const size_t cut = code.find('_');
	return cut == std::string::npos ? code : code.substr(0, cut);
}

std::mutex g_lock;
std::shared_ptr<const Table> g_table;   /* the language in use; null: none read yet */
std::string g_code = "en";

std::shared_ptr<const Table> tableInUse()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_table != nullptr) return g_table;
	}
	/* English until told otherwise, read at the first text asked for */
	auto table = std::make_shared<Table>();
	std::string error;
	readTableFile("en", *table, error);
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_table == nullptr) g_table = table;
	return g_table;
}

} // namespace

const Table::Entry *Table::find(const char *key) const
{
	auto at = std::lower_bound(entries.begin(), entries.end(), key,
		[](const std::pair<std::string, Entry> &e, const char *k) { return std::strcmp(e.first.c_str(), k) < 0; });
	return at != entries.end() && at->first == key ? &at->second : nullptr;
}

bool readTable(const char *json, size_t length, Table &out, std::string &error)
{
	cJSON *root = cJSON_ParseWithLength(json, length);
	if (root == nullptr || !cJSON_IsObject(root))
	{
		cJSON_Delete(root);
		error = "not a JSON object";
		return false;
	}
	const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
	const cJSON *english = cJSON_GetObjectItemCaseSensitive(root, "english");
	const cJSON *texts = cJSON_GetObjectItemCaseSensitive(root, "texts");
	if (!cJSON_IsString(name) || !cJSON_IsString(english) || !cJSON_IsObject(texts))
	{
		cJSON_Delete(root);
		error = "it needs \"name\", \"english\" and \"texts\"";
		return false;
	}
	out.name = name->valuestring;
	out.english = english->valuestring;
	out.entries.clear();
	for (const cJSON *t = texts->child; t != nullptr; t = t->next)
	{
		Table::Entry entry;
		if (cJSON_IsString(t))
		{
			entry.text = t->valuestring;
		}
		else if (cJSON_IsObject(t) && t->child != nullptr)
		{
			for (const cJSON *f = t->child; f != nullptr; f = f->next)
			{
				if (!cJSON_IsString(f))
				{
					error = std::string("\"") + t->string + "\": a plural form that is not a text";
					cJSON_Delete(root);
					return false;
				}
				entry.forms.emplace_back(f->string, f->valuestring);
			}
		}
		else
		{
			error = std::string("\"") + t->string + "\" is neither a text nor plural forms";
			cJSON_Delete(root);
			return false;
		}
		out.entries.emplace_back(t->string, std::move(entry));
	}
	cJSON_Delete(root);
	std::sort(out.entries.begin(), out.entries.end(),
		[](const std::pair<std::string, Table::Entry> &a, const std::pair<std::string, Table::Entry> &b) { return a.first < b.first; });
	for (size_t i = 1; i < out.entries.size(); i++)
	{
		if (out.entries[i].first == out.entries[i - 1].first)
		{
			error = "\"" + out.entries[i].first + "\" is there twice";
			return false;
		}
	}
	return true;
}

std::string folder()
{
	return folderOfUs() + "languages/";
}

bool readTableFile(const std::string &code, Table &out, std::string &error)
{
	std::vector<uint8_t> bytes;
	const std::string path = folder() + code + ".json";
	if (!readFile(path.c_str(), bytes))
	{
		error = path + " could not be read";
		return false;
	}
	if (!readTable(reinterpret_cast<const char *>(bytes.data()), bytes.size(), out, error))
	{
		error = path + ": " + error;
		return false;
	}
	return true;
}

const std::vector<Language> &languages()
{
	static const std::vector<Language> all = [] {
		std::vector<Language> out;
		std::error_code ec;
		for (const auto &entry : std::filesystem::directory_iterator(std::filesystem::u8path(folder()), ec))
		{
			if (entry.path().extension() != ".json") continue;
			const std::string code = entry.path().stem().u8string();
			Table table;
			std::string error;
			if (readTableFile(code, table, error)) out.push_back({code, table.name, table.english});
		}
		std::sort(out.begin(), out.end(), [](const Language &a, const Language &b) {
			if ((a.code == "en") != (b.code == "en")) return a.code == "en";
			return a.code < b.code;
		});
		return out;
	}();
	return all;
}

bool use(const std::string &code)
{
	const auto &all = languages();
	if (std::none_of(all.begin(), all.end(), [&](const Language &l) { return l.code == code; })) return false;
	auto table = std::make_shared<Table>();
	std::string error;
	if (!readTableFile(code, *table, error)) return false;
	std::lock_guard<std::mutex> lock(g_lock);
	g_table = table;
	g_code = code;
	return true;
}

std::string inUse()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_code;
}

/* CLDR's plural rules, for whole numbers, of the languages Chimera has. A
 * language added needs its rule here. */
std::vector<std::string> pluralForms(const std::string &code)
{
	const std::string base = baseOf(code);
	if (base == "ja" || base == "zh" || base == "ko" || base == "id" || base == "vi") return {"other"};
	if (base == "ru" || base == "uk" || base == "pl") return {"one", "few", "many"};
	if (base == "cs" || base == "ro") return {"one", "few", "other"};
	if (base == "en" || base == "de" || base == "es" || base == "it" || base == "nl" || base == "sv" || base == "da"
		|| base == "nb" || base == "fi" || base == "el" || base == "hu" || base == "tr" || base == "ca"
		|| base == "fr" || base == "pt")
	{
		return {"one", "other"};
	}
	return {};
}

std::string pluralForm(const std::string &code, int64_t count)
{
	const std::string base = baseOf(code);
	const uint64_t n = count < 0 ? 0 - static_cast<uint64_t>(count) : static_cast<uint64_t>(count);
	const uint64_t n10 = n % 10, n100 = n % 100;
	const std::vector<std::string> forms = pluralForms(code);
	if (forms.size() == 1) return "other";
	if (base == "ru" || base == "uk")
	{
		if (n10 == 1 && n100 != 11) return "one";
		if (n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14)) return "few";
		return "many";
	}
	if (base == "pl")
	{
		if (n == 1) return "one";
		if (n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14)) return "few";
		return "many";
	}
	if (base == "cs")
	{
		if (n == 1) return "one";
		if (n >= 2 && n <= 4) return "few";
		return "other";
	}
	if (base == "ro")
	{
		if (n == 1) return "one";
		if (n == 0 || (n100 >= 2 && n100 <= 19)) return "few";
		return "other";
	}
	if (base == "fr" || base == "pt") return n <= 1 ? "one" : "other";
	return n == 1 ? "one" : "other";
}

std::string fill(const std::string &pattern, const std::vector<std::string> &args)
{
	std::string out;
	out.reserve(pattern.size());
	for (size_t i = 0; i < pattern.size(); i++)
	{
		const char c = pattern[i];
		if (c == '%' && i + 1 < pattern.size() && pattern[i + 1] >= '1' && pattern[i + 1] <= '9')
		{
			const size_t index = static_cast<size_t>(pattern[i + 1] - '1');
			if (index < args.size())
			{
				out += args[index];
				i++;
				continue;
			}
		}
		out += c;
	}
	return out;
}

std::string text(const char *key, std::initializer_list<std::string> args)
{
	return text(key, -1, args);
}

std::string text(const char *key, int64_t count, std::initializer_list<std::string> args)
{
	const std::shared_ptr<const Table> table = tableInUse();
	const Table::Entry *entry = table->find(key);
	if (entry == nullptr) return key;
	std::string pattern = entry->text;
	if (!entry->forms.empty())
	{
		const std::string form = count < 0 ? std::string("other") : pluralForm(inUse(), count);
		pattern = entry->forms.back().second;
		for (const auto &f : entry->forms)
		{
			if (f.first == form) { pattern = f.second; break; }
		}
		const std::string number = std::to_string(count < 0 ? 0 : count);
		for (size_t at = pattern.find("%n"); at != std::string::npos; at = pattern.find("%n", at + number.size()))
		{
			pattern.replace(at, 2, number);
		}
	}
	return fill(pattern, std::vector<std::string>(args));
}

} // namespace chimera::lang

/* ---- the ABI (engine.h) ---- */

namespace {
thread_local chimera::ThreadString g_text;   /* never destroyed: see thread_string.hpp */
thread_local chimera::ThreadString g_code;   /* likewise */
}

int32_t ce_language_count(void)
{
	return static_cast<int32_t>(chimera::lang::languages().size());
}

const char *ce_language_code(int32_t index)
{
	const auto &all = chimera::lang::languages();
	return index >= 0 && static_cast<size_t>(index) < all.size() ? all[static_cast<size_t>(index)].code.c_str() : "";
}

const char *ce_language_name(int32_t index)
{
	const auto &all = chimera::lang::languages();
	return index >= 0 && static_cast<size_t>(index) < all.size() ? all[static_cast<size_t>(index)].name.c_str() : "";
}

const char *ce_language_english_name(int32_t index)
{
	const auto &all = chimera::lang::languages();
	return index >= 0 && static_cast<size_t>(index) < all.size() ? all[static_cast<size_t>(index)].english.c_str() : "";
}

int32_t ce_language_use(const char *code)
{
	return code != nullptr && chimera::lang::use(code) ? 1 : 0;
}

const char *ce_language_in_use(void)
{
	*g_code = chimera::lang::inUse();
	return g_code->c_str();
}

const char *ce_text(const char *key, int64_t count, const char *const *args, int32_t arg_count)
{
	if (key == nullptr) key = "";
	std::vector<std::string> filled;
	for (int32_t i = 0; args != nullptr && i < arg_count && i < 9; i++) filled.emplace_back(args[i] != nullptr ? args[i] : "");
	/* the pattern first, then the arguments: an argument's own %1 is not filled */
	const std::string pattern = chimera::lang::text(key, count);
	*g_text = chimera::lang::fill(pattern, filled);
	return g_text->c_str();
}
