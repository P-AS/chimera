/* game_properties.hpp - a game core's properties (docs/game-cores.md).
 *
 * A game core describes named places in its memory domains - the Kid's
 * position, a level's name, an array of guards - in a JSON table from its
 * GetGameProperties export. This is the table checked against the domains the
 * core really has, and every value read, written, shown and parsed by one set
 * of rules, so a watch, a poke, a freeze and a script agree on what the bytes
 * mean, and anything that links the engine gets the same properties.
 *
 * Memory is reached through each domain's pointer, which is stable for a
 * session's lifetime; a domain without one cannot hold a property.
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

class CeGameProperties
{
public:
	/* The numbering of Value::Kind is the ABI's (engine.h, CE_PROPERTY_*). */
	enum Type { U8 = 0, S8, U16, S16, U32, S32, U64, S64, F32, F64, Bool, String, Bytes };
	enum Encoding { Ascii = 0, Latin1, Utf8, Utf16le };

	struct Domain
	{
		std::string name;
		uint8_t *base = nullptr;
		int64_t size = 0;
		bool writable = false;
	};

	struct Property
	{
		std::string name, domain, group, description;
		int32_t domainIndex = -1;
		int64_t offset = 0;
		Type type = U8;
		uint32_t size = 1;   // bytes in one element
		uint32_t count = 1;  // elements; 1 is not an array
		uint32_t first = 0;  // the number the first element is called by (Rooms[1] when the game counts from 1)
		uint32_t stride = 1; // bytes from one element to the next
		Encoding encoding = Ascii;
		bool bigEndian = false;
		bool writable = true;
		uint32_t bit = 0, bits = 0; // a bit field when bits is not 0
		std::vector<std::pair<int64_t, std::string>> values;

		int64_t elementOffset(uint32_t element) const { return offset + (int64_t)element * stride; }
		int64_t span() const { return (int64_t)(count - 1) * stride + size; }
	};

	/* A value on its way in or out; `kind` says which member holds it. */
	struct Value
	{
		enum Kind { Int = 0, UInt, Float, Boolean, Text, Raw };
		Kind kind = Int;
		int64_t i = 0;     // Int, and Boolean as 0 or 1
		uint64_t u = 0;    // UInt
		double f = 0;      // Float
		std::string data;  // Text as UTF-8, or Raw bytes
	};

	/* Reads a table, keeping what can be used and saying why the rest cannot. */
	void load(const char *json, const std::vector<Domain> &domains);

	const std::vector<Property> &all() const { return m_props; }
	const std::vector<std::string> &problems() const { return m_problems; }

	/* The table as the engine understood it, every field filled in, and what
	 * was left out: {"properties": [...], "problems": [...]}. */
	const std::string &describe() const { return m_describe; }

	/* "Name" or "Name[3]", any case: the property, and the element, counted
	 * from 0 whatever the array's `first` (0 for a name without an index). The
	 * number in the name is the game's - `first` for the first element. -1 for
	 * a name there is not, a number outside the array, or an index on a
	 * property that is not an array. */
	int32_t find(const std::string &name, uint32_t *element) const;

	/* The first property, in the table's order, one of whose elements covers
	 * `address` in the named domain; -1 when none does. `starts` says whether
	 * the address is the element's first byte. */
	int32_t at(const std::string &domain, int64_t address, uint32_t *element, bool *starts) const;

	bool read(int32_t index, uint32_t element, Value &out) const;
	bool write(int32_t index, uint32_t element, const Value &in, std::string &error) const;

	/* A value as a person reads it: an enumeration's name where it has one
	 * (when `named`), floats at the fewest digits that read back the same,
	 * text as UTF-8, bytes in hex. */
	std::string text(int32_t index, uint32_t element, bool named) const;

	/* The inverse of text(): a number (decimal, or hex after 0x), an
	 * enumeration's name, true/false, text, or hex bytes. */
	bool writeText(int32_t index, uint32_t element, const std::string &text, std::string &error) const;

	static const char *typeName(Type type);

private:
	std::vector<Domain> m_domains;
	std::vector<Property> m_props;
	std::vector<std::string> m_problems;
	std::map<std::string, int32_t> m_byName; // lower-cased
	std::string m_describe;

	bool valid(int32_t index, uint32_t element) const;
	uint8_t *elementBytes(const Property &p, uint32_t element) const;
	uint64_t readRaw(const Property &p, const uint8_t *at) const;
	void writeRaw(const Property &p, uint8_t *at, uint64_t raw) const;
	void describeAll();
};
