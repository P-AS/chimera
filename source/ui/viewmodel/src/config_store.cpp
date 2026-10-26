#include "chimera/vm/config_store.hpp"
#include "chimera/vm/chimera_settings.hpp"

#include <charconv>
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
		/* a whole number too big for 64 bits is kept as the nearest double; std::stoll threw and ended the program */
		if (j.text.find_first_of(".eE") == std::string::npos)
		{
			int64_t whole = 0;
			const auto [end, ec] = std::from_chars(j.text.data(), j.text.data() + j.text.size(), whole);
			if (ec == std::errc() && end == j.text.data() + j.text.size()) return whole;
		}
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

bool ConfigStore::switchIn(const std::string &path, const std::string &key, bool otherwise)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) return otherwise;
	std::stringstream ss;
	ss << in.rdbuf();
	Json all;
	std::string error;
	if (!json::parse(ss.str(), all, error)) return otherwise;
	const Json *on = all.find(key);
	return on != nullptr && on->kind == Json::Kind::Bool ? on->boolean : otherwise;
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
		kept_.push_back({&vm, property, key});
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

bool ConfigStore::saveAs(const std::string &path, std::string &error)
{
	std::error_code ec;
	const std::filesystem::path p(path);
	if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << json::write(root_) << "\n";
	if (!out)
	{
		error = "cannot write " + path;
		return false;
	}
	return true;
}

int64_t ConfigStore::loadFrom(const std::string &path, std::string &error)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		error = "cannot read " + path;
		return -1;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	Json read;
	std::string why;
	if (!json::parse(ss.str(), read, why) || read.kind != Json::Kind::Object)
	{
		error = path + " is not a settings file (" + (why.empty() ? std::string("not an object") : why) + ")";
		return -1;
	}
	/* Chimera's own config.ini, read as the window's settings */
	if (isChimeraSettings(read))
	{
		ChimeraImport imported;
		if (!importChimeraSettings(ss.str(), imported, why))
		{
			error = path + " is not a settings file (" + why + ")";
			return -1;
		}
		read = std::move(imported.settings);
	}
	int64_t set = 0;
	for (const Kept &k : kept_)
		if (const Json *v = read.find(k.key))
		{
			std::string refused;
			/* a value the property refuses is left as it is */
			if (k.vm->set(k.property, fromJson(*v), refused)) set++;
		}
	return set;
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
