#pragma once
/* Chimera's languages (docs/translations.md).
 *
 * Every text Chimera shows a person is a key in a language table, one table
 * per language, English among them (owner, 2026-10-28): languages/<code>.json
 * in the source, read at run time from the languages folder beside the
 * engine. The window and the view models read them through ce_text, and the
 * engine's own messages through text() here. A key no table has comes back
 * as the key itself, so a missing one is seen rather than covered: the
 * tests require every table to have every key. */

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace chimera::lang {

struct Language
{
	std::string code;      /* "de", "pt", "zh_CN" */
	std::string name;      /* in that language: "Deutsch" */
	std::string english;   /* "German" */
};

/* the languages folder beside the engine, ending in a slash */
std::string folder();

/* the languages there, English first, then by code; read once */
const std::vector<Language> &languages();

/* Chimera's texts in this language from now on; false when there is no
 * table for it, or it cannot be read (and the one in use stays) */
bool use(const std::string &code);
std::string inUse();

/* a key's text in the language in use, with %1 to %9 replaced by args and,
 * for a key with plural forms, the form for count chosen and its %n replaced
 * by count (a count below zero: the "other" form) */
std::string text(const char *key, std::initializer_list<std::string> args = {});
std::string text(const char *key, int64_t count, std::initializer_list<std::string> args = {});

/* The plural forms a language's texts give, by the names CLDR uses (one,
 * few, many, other), for whole numbers; empty for a language with no rule
 * here, whose table the tests refuse. */
std::vector<std::string> pluralForms(const std::string &code);
/* which of them a count takes */
std::string pluralForm(const std::string &code, int64_t count);

/* %1 to %9 in a pattern replaced by args; a % not followed by one of those
 * digits stays as it is */
std::string fill(const std::string &pattern, const std::vector<std::string> &args);

/* A table, read from its JSON: false and why when it is not one. */
struct Table
{
	std::string name, english;
	struct Entry
	{
		std::string text;                                       /* no plural forms */
		std::vector<std::pair<std::string, std::string>> forms; /* form name, text */
	};
	std::vector<std::pair<std::string, Entry>> entries;        /* sorted by key */
	const Entry *find(const char *key) const;
};
bool readTable(const char *json, size_t length, Table &out, std::string &error);
/* the one in the languages folder for that code */
bool readTableFile(const std::string &code, Table &out, std::string &error);

} // namespace chimera::lang
