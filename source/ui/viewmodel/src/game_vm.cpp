#include "chimera/vm/game_vm.hpp"

#include "chimera/engine.h"
#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace {
/* the property a name is, in the table; nullptr for none */
const json::Json *propertyOf(const json::Json &table, int32_t index)
{
	const json::Json *list = table.find("properties");
	if (list == nullptr || index < 0 || static_cast<size_t>(index) >= list->items.size()) return nullptr;
	return &list->items[static_cast<size_t>(index)];
}

std::string textOf(const json::Json *p, const char *member)
{
	if (p == nullptr) return {};
	const json::Json *m = p->find(member);
	if (m == nullptr) return {};
	if (m->kind == json::Json::Kind::Bool) return m->boolean ? "true" : "false";
	return m->text;
}
} // namespace

GamePropertiesViewModel::GamePropertiesViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("game", dispatcher), session_(session)
{
	expose("lastError", "why the last get or set did not happen", lastError);
	command("list", "the game properties' names, a line each, in the core's order (an array once, by its own name)", {},
		[this](const Args &, std::string &) -> Value {
			std::string out;
			session_.onMachineWait([&](ce_session *s) {
				if (s == nullptr) return;
				/* a dynamic table is asked for as it is now: what a movie keeps changes as it runs */
				if (ce_session_property_dynamic(s) != 0) ce_session_property_refresh(s);
				json::Json table;
				std::string err;
				const char *t = ce_session_property_table(s);
				if (t == nullptr || !json::parse(t, table, err)) return;
				if (const json::Json *list = table.find("properties"))
					for (const json::Json &p : list->items)
						if (textOf(&p, "listed") != "false") out += (out.empty() ? "" : "\n") + textOf(&p, "name");
			});
			return out;
		});
	command("kind", "what a property's value is: int, uint, float, bool, text or bytes; \"\" for none", {"name"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name, kind;
			if (!arg(a, 0, name, error)) return {};
			session_.onMachineWait([&](ce_session *s) {
				uint32_t element = 0;
				const int32_t index = s != nullptr ? ce_session_property_find(s, name.c_str(), &element) : -1;
				ce_property_value v{};
				if (index < 0 || ce_session_property_get(s, index, element, &v) != 0) return;
				static const char *const kinds[] = {"int", "uint", "float", "bool", "text", "bytes"};
				if (v.kind >= 0 && v.kind < 6) kind = kinds[v.kind];
			});
			return kind;
		});
	command("get", "a property's value (\"Name\" or \"Name[2]\"); nothing for a name the core does not have", {"name"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			Value out;
			std::string why;
			session_.onMachineWait([&](ce_session *s) {
				uint32_t element = 0;
				const int32_t index = s != nullptr ? ce_session_property_find(s, name.c_str(), &element) : -1;
				if (index < 0)
				{
					why = "the game has no property " + name;
					return;
				}
				ce_property_value v{};
				if (ce_session_property_get(s, index, element, &v) != 0)
				{
					why = ce_session_last_error(s);
					return;
				}
				switch (v.kind)
				{
				case CE_PROPERTY_INT: out = v.i; break;
				/* Lua's integers are 64 bits wide and signed: a u64 as the integer with the same bits */
				case CE_PROPERTY_UINT: out = static_cast<int64_t>(v.u); break;
				case CE_PROPERTY_FLOAT: out = v.f; break;
				case CE_PROPERTY_BOOL: out = v.i != 0; break;
				case CE_PROPERTY_TEXT:
				case CE_PROPERTY_BYTES: out = std::string(v.data != nullptr ? v.data : "", static_cast<size_t>(std::max<int64_t>(0, v.len))); break;
				default: break;
				}
			});
			lastError.set(why);
			return out;
		});
	command("set", "sets a property; the game's next step sees it. A number, a boolean, a string, or (asBytes) the bytes "
		"of a string; whether it was set, and lastError why not", {"name", "value", "asBytes"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			if (a.size() < 2)
			{
				error = "argument 2 is missing";
				return {};
			}
			const Value value = a[1];
			bool asBytes = false;
			if (a.size() > 2) arg(a, 2, asBytes, error);
			bool done = false;
			std::string why;
			session_.onMachineWait([&](ce_session *s) {
				uint32_t element = 0;
				const int32_t index = s != nullptr ? ce_session_property_find(s, name.c_str(), &element) : -1;
				if (index < 0)
				{
					why = "the game has no property " + name;
					return;
				}
				ce_property_value v{};
				std::string text;
				if (const bool *b = std::get_if<bool>(&value))
				{
					v.kind = CE_PROPERTY_BOOL;
					v.i = *b ? 1 : 0;
				}
				else if (const int64_t *i = std::get_if<int64_t>(&value))
				{
					v.kind = CE_PROPERTY_INT;
					v.i = *i;
				}
				else if (const double *f = std::get_if<double>(&value))
				{
					v.kind = CE_PROPERTY_FLOAT;
					v.f = *f;
				}
				else if (const std::string *t = std::get_if<std::string>(&value))
				{
					text = *t;
					v.kind = asBytes ? CE_PROPERTY_BYTES : CE_PROPERTY_TEXT;
					v.data = text.data();
					v.len = static_cast<int64_t>(text.size());
				}
				else
				{
					why = "no value to set " + name + " to";
					return;
				}
				if (ce_session_property_set(s, index, element, &v) == 0) done = true;
				else why = ce_session_last_error(s);
			});
			lastError.set(why);
			return done;
		});
	command("describe", "what a property is, a \"key\\tvalue\" line each: name, domain, offset (where it is now), type, size, count, "
		"first, stride, endian, encoding, bit, bits, group, writable, description, label; \"\" for a name the core does not have",
		{"name"}, [this](const Args &a, std::string &error) -> Value {
			std::string name, out;
			if (!arg(a, 0, name, error)) return {};
			session_.onMachineWait([&](ce_session *s) {
				uint32_t element = 0;
				const int32_t index = s != nullptr ? ce_session_property_find(s, name.c_str(), &element) : -1;
				if (index < 0) return;
				const int64_t offset = ce_session_property_offset(s, index, element);
				char label[512] = {0};
				const int32_t n = ce_session_property_text(s, index, element, 1, label, sizeof label);
				json::Json table;
				std::string err;
				const char *t = ce_session_property_table(s);
				if (t == nullptr || !json::parse(t, table, err)) return;
				const json::Json *p = propertyOf(table, index);
				if (p == nullptr) return;
				/* the element named, or the property's own name */
				out = "name\t" + name;
				out += "\ndomain\t" + textOf(p, "domain") + "\noffset\t" + std::to_string(offset) + "\ntype\t" + textOf(p, "type");
				for (const char *key : {"size", "count", "first", "stride", "endian", "encoding", "bit", "bits", "group", "writable", "description"})
					out += "\n" + std::string(key) + "\t" + textOf(p, key);
				/* the value as the core names it, when it has names for its values */
				const json::Json *values = p->find("values");
				if (n >= 0 && values != nullptr && !values->members.empty()) out += "\nlabel\t" + std::string(label);
			});
			return out;
		});
}

} // namespace chimera::vm
