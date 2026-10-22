#include "chimera/vm/config_store.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace chimera::vm {

using json::Json;

namespace {
Json toJson(const Value &v)
{
	if (const bool *b = std::get_if<bool>(&v)) return Json::boolean_(*b);
	if (const int64_t *i = std::get_if<int64_t>(&v)) return Json::integer(*i);
	if (const double *d = std::get_if<double>(&v)) return Json::number(*d);
	if (const std::string *s = std::get_if<std::string>(&v)) return Json::string(*s);
	return Json();
}
Value fromJson(const Json &j)
{
	switch (j.kind)
	{
	case Json::Kind::Bool: return j.boolean;
	case Json::Kind::Number:
		if (j.text.find_first_of(".eE") == std::string::npos) return static_cast<int64_t>(std::stoll(j.text));
		return j.number();
	case Json::Kind::String: return j.text;
	default: return {};
	}
}
} // namespace

ConfigStore::ConfigStore(std::string path) : path_(std::move(path))
{
	root_.kind = Json::Kind::Object;
	std::ifstream in(path_, std::ios::binary);
	if (!in) return;
	std::stringstream ss;
	ss << in.rdbuf();
	in.close();
	std::string error;
	Json read;
	if (json::parse(ss.str(), read, error) && read.kind == Json::Kind::Object)
	{
		root_ = std::move(read);
		return;
	}
	/* kept for whoever wants to look, and not written over */
	std::error_code ec;
	std::filesystem::rename(path_, path_ + ".unreadable", ec);
	problem_ = "the settings file did not read (" + error + "); it was set aside as " + path_ + ".unreadable";
}

void ConfigStore::keep(ViewModel &vm, const std::string &property)
{
	const std::string key = vm.name() + "." + property;
	for (const auto &p : vm.properties())
	{
		if (p.name != property) continue;
		if (const Json *saved = root_.find(key); saved != nullptr && p.set)
		{
			std::string error;
			vm.set(property, fromJson(*saved), error); /* a value the property refuses is left as it is */
		}
		auto get = p.get;
		watches_.push_back(p.changed->connect([this, key, get] {
			const Json now = toJson(get());
			bool found = false;
			for (auto &[k, v] : root_.members)
				if (k == key)
				{
					v = now;
					found = true;
				}
			if (!found) root_.members.emplace_back(key, now);
			save();
		}));
		return;
	}
}

void ConfigStore::save()
{
	std::error_code ec;
	const std::filesystem::path p(path_);
	if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
	/* written beside and moved into place: a failed write leaves the last good file */
	const std::string temp = path_ + ".new";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		out << json::write(root_) << "\n";
		if (!out)
		{
			problem_ = "cannot write " + temp;
			return;
		}
	}
	std::filesystem::rename(temp, path_, ec);
	problem_ = ec ? "cannot write " + path_ + ": " + ec.message() : std::string();
}

} // namespace chimera::vm
