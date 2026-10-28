/* test_language.cpp - Chimera's language tables (docs/translations.md).
 *
 * Every text Chimera shows is a key in one table per language, and every
 * language has every key (owner, 2026-10-28). This checks the tables in the
 * languages folder against English - the same keys, the same %1..%9 and %n, a menu's
 * shortcut letter kept, the spaces at either end kept, the plural forms the
 * language takes - and what reads them: the plural rules, the filling, a
 * language used and another in its place, and the C ABI.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/language.hpp"
#include "chimera/engine.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

namespace lang = chimera::lang;

/* the %1..%9 and %n a text uses */
std::multiset<std::string> placeholders(const std::string &t)
{
	std::multiset<std::string> out;
	for (size_t i = 0; i + 1 < t.size(); i++)
	{
		if (t[i] == '%' && ((t[i + 1] >= '1' && t[i + 1] <= '9') || t[i + 1] == 'n')) out.insert(t.substr(i, 2));
	}
	return out;
}

/* a menu's shortcut letter: an & before a letter, not "&&" */
bool hasShortcut(const std::string &t)
{
	for (size_t i = 0; i + 1 < t.size(); i++)
	{
		if (t[i] != '&') continue;
		if (t[i + 1] == '&') { i++; continue; }
		if (t[i + 1] != ' ') return true;
	}
	return false;
}

std::string edges(const std::string &t)
{
	size_t a = 0, b = t.size();
	while (a < b && (t[a] == ' ' || t[a] == '\n')) a++;
	while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\n')) b--;
	return t.substr(0, a) + "|" + t.substr(b);
}

int problems = 0;
void problem(const std::string &code, const std::string &key, const std::string &what)
{
	if (problems++ < 40) std::fprintf(stderr, "%s: %s: %s\n", code.c_str(), key.c_str(), what.c_str());
}

} // namespace

int main()
{
	{ // a table is read, and what is not one is said
		lang::Table t;
		std::string error;
		const char good[] = R"({"name": "Test", "english": "Test", "texts": {"b.two": "Two", "a.one": "One",
			"c.frames": {"one": "%n frame", "other": "%n frames"}}})";
		assert(lang::readTable(good, sizeof good - 1, t, error));
		assert(t.name == "Test" && t.entries.size() == 3);
		assert(t.entries[0].first == "a.one");   /* sorted */
		assert(t.find("b.two") != nullptr && t.find("b.two")->text == "Two");
		assert(t.find("c.frames")->forms.size() == 2 && t.find("c.frames")->forms[0].first == "one");
		assert(t.find("d.none") == nullptr);
		const char *bad[] = {
			"[]",
			R"({"name": "x", "texts": {}})",
			R"({"name": "x", "english": "x", "texts": {"a": 3}})",
			R"({"name": "x", "english": "x", "texts": {"a": {"one": 1}}})",
			R"({"name": "x", "english": "x", "texts": {"a": "1", "a": "2"}})",
		};
		for (const char *b : bad)
		{
			error.clear();
			assert(!lang::readTable(b, std::strlen(b), t, error));
			assert(!error.empty());
		}
	}

	{ // the plural rules, whole numbers
		assert(lang::pluralForm("en", 1) == "one" && lang::pluralForm("en", 0) == "other" && lang::pluralForm("en", 2) == "other");
		assert(lang::pluralForm("fr", 0) == "one" && lang::pluralForm("fr", 1) == "one" && lang::pluralForm("fr", 2) == "other");
		assert(lang::pluralForm("pt", 0) == "one" && lang::pluralForm("pt", 2) == "other");
		assert(lang::pluralForm("ja", 1) == "other" && lang::pluralForm("zh_CN", 5) == "other");
		const int ru[][2] = {{1, 0}, {21, 0}, {101, 0}, {11, 2}, {2, 1}, {3, 1}, {4, 1}, {22, 1}, {12, 2}, {14, 2}, {5, 2}, {0, 2}, {111, 2}};
		const char *ruForms[] = {"one", "few", "many"};
		for (const auto &c : ru)
		{
			assert(lang::pluralForm("ru", c[0]) == ruForms[c[1]]);
			assert(lang::pluralForm("uk", c[0]) == ruForms[c[1]]);
		}
		assert(lang::pluralForm("pl", 1) == "one" && lang::pluralForm("pl", 21) == "many" && lang::pluralForm("pl", 22) == "few");
		assert(lang::pluralForm("cs", 1) == "one" && lang::pluralForm("cs", 4) == "few" && lang::pluralForm("cs", 5) == "other");
		assert(lang::pluralForm("ro", 0) == "few" && lang::pluralForm("ro", 19) == "few" && lang::pluralForm("ro", 20) == "other"
			&& lang::pluralForm("ro", 102) == "few");
		assert(lang::pluralForms("xx").empty());
	}

	{ // filling
		assert(lang::fill("%1 of %2", {"3", "4"}) == "3 of 4");
		assert(lang::fill("%2, then %1", {"a", "b"}) == "b, then a");
		assert(lang::fill("100% done, %3 left", {"a"}) == "100% done, %3 left");
		assert(lang::fill("%1", {"%2"}) == "%2");   /* what fills is not filled again */
	}

	{ // every table there has every key English has, and nothing else
		const auto &all = lang::languages();
		assert(all.size() >= 8 && all.front().code == "en");
		std::vector<lang::Table> tables(all.size());
		for (size_t i = 0; i < all.size(); i++)
		{
			std::string error;
			if (!lang::readTableFile(all[i].code, tables[i], error)) problem(all[i].code, "(the table)", error);
		}
		const lang::Table *english = &tables[0];
		for (size_t i = 0; i < tables.size(); i++)
		{
			const std::string code = all[i].code;
			const std::vector<std::string> forms = lang::pluralForms(code);
			if (forms.empty()) problem(code, "(the table)", "no plural rule for this language (language.cpp)");
			for (const auto &e : english->entries)
			{
				const lang::Table::Entry *t = tables[i].find(e.first.c_str());
				if (t == nullptr) { problem(code, e.first, "missing"); continue; }
				const std::string en = e.second.forms.empty() ? e.second.text : e.second.forms.back().second;
				if (e.second.forms.empty() != t->forms.empty()) { problem(code, e.first, "plural forms where English has none, or none where it has"); continue; }
				std::vector<std::string> texts;
				if (t->forms.empty()) texts.push_back(t->text);
				else
				{
					std::vector<std::string> named;
					for (const auto &f : t->forms) { named.push_back(f.first); texts.push_back(f.second); }
					if (named != forms) problem(code, e.first, "plural forms are not " + std::to_string(forms.size()) + " in the language's order");
				}
				for (const std::string &x : texts)
				{
					std::multiset<std::string> want = placeholders(en), have = placeholders(x);
					/* a language with one form may leave %n out of none; one form in a
					 * language with several may write the number as a word ("one") */
					if (!e.second.forms.empty()) { want.erase("%n"); have.erase("%n"); }
					if (want != have) problem(code, e.first, "placeholders differ from English: " + x);
					if (hasShortcut(en) && !hasShortcut(x)) problem(code, e.first, "no shortcut letter (&): " + x);
					if (edges(en) != edges(x)) problem(code, e.first, "spaces or line breaks at the ends differ from English");
				}
			}
			for (const auto &e : tables[i].entries)
			{
				if (english->find(e.first.c_str()) == nullptr) problem(code, e.first, "not an English key");
			}
		}
		if (problems != 0) std::fprintf(stderr, "%d problems in the language tables\n", problems);
		assert(problems == 0);
	}

	{ // used, and another in its place; a key no table has comes back as itself
		assert(lang::inUse() == "en");
		assert(lang::text("common.file") == "&File");
		assert(lang::text("no.such.key") == "no.such.key");
		assert(lang::use("es"));
		assert(lang::text("common.file") == "&Archivo");
		assert(!lang::use("xx"));
		assert(lang::inUse() == "es" && lang::text("common.file") == "&Archivo");
		assert(lang::use("en"));
	}

	{ // the C ABI
		assert(ce_language_count() == static_cast<int32_t>(lang::languages().size()));
		assert(std::strcmp(ce_language_code(0), "en") == 0 && std::strcmp(ce_language_name(0), "English") == 0);
		bool german = false;
		for (int32_t i = 0; i < ce_language_count(); i++)
		{
			if (std::strcmp(ce_language_code(i), "de") == 0)
				german = std::strcmp(ce_language_name(i), "Deutsch") == 0 && std::strcmp(ce_language_english_name(i), "German") == 0;
		}
		assert(german);
		assert(std::strcmp(ce_language_code(-1), "") == 0 && std::strcmp(ce_language_code(ce_language_count()), "") == 0);
		assert(ce_language_use("fr") == 1 && std::strcmp(ce_language_in_use(), "fr") == 0);
		assert(ce_language_use("xx") == 0 && std::strcmp(ce_language_in_use(), "fr") == 0);
		assert(std::strcmp(ce_text("common.file", -1, nullptr, 0), "&Fichier") == 0);
		const char *args[] = {"60"};
		assert(std::strcmp(ce_language_use("en") == 1 ? ce_text("display_config.framerate_hz", -1, args, 1) : "", "Framerate: 60 Hz") == 0);
	}

	std::printf("language tables: %zu, every key in every one\n", lang::languages().size());
	return 0;
}
