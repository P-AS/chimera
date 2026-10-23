#include "chimera/vm/paths_vm.hpp"

#include <algorithm>
#include <filesystem>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace fs = std::filesystem;

const std::vector<std::pair<std::string, std::string>> &PathsViewModel::defaults()
{
	/* Chimera's global entries (PathEntryCollection) that the prototype uses,
	 * and Screenshots, which Chimera keeps per system */
	static const std::vector<std::pair<std::string, std::string>> list = {
		{"ROM", "."},
		{"Firmware", "./Firmware"},
		{"Movies", "./Movies"},
		{"Movie backups", "./Movies/backup"},
		/* where Encode Video suggests a video goes */
		{"A/V Dumps", "."},
		{"Tools", "./Tools"},
		{"Screenshots", "./Screenshots"},
		{"Lua", "./Lua"},
		{"Themes", "./Themes"},
		/* Watch and Debug Logs are under Tools when relative, as Chimera resolves them */
		{"Watch (.wch)", "."},
		{"Debug Logs", "."},
		{"Bug reports", "./Reports"},
		{"Macros", "./Movies/Macros"},
	};
	return list;
}

PathsViewModel::PathsViewModel(Dispatcher &dispatcher) : ViewModel("paths", dispatcher)
{
	expose("base", "the folder relative paths start from; \"\" the program's own", base, true);
	expose("programFolder", "the program's folder", programFolder);
	expose("changed", "the paths changed from Chimera's, {\"Movies\": \"D:/runs\", ...}", changed, true);
	expose("recentForRoms", "the game dialog opens where the last game came from", recentForRoms, true);
	expose("lastRomFolder", "where the last game came from", lastRomFolder, true);
	rows("paths", "one row a folder: name, path (as set), folder (made absolute), isDefault", rows_);
	command("setPath", "a folder's path, absolute or relative to the base; \"\" puts Chimera's back", {"name", "path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name, path;
			if (!arg(a, 0, name, error) || !arg(a, 1, path, error)) return {};
			const auto &d = defaults();
			const auto it = std::find_if(d.begin(), d.end(), [&](const auto &e) { return e.first == name; });
			if (it == d.end())
			{
				error = "no folder called " + name;
				return {};
			}
			json::Json all;
			std::string err;
			if (!json::parse(changed.get(), all, err) || all.kind != json::Json::Kind::Object)
			{
				all = json::Json();
				all.kind = json::Json::Kind::Object;
			}
			std::erase_if(all.members, [&](const auto &m) { return m.first == name; });
			/* Chimera's own path is not a change */
			if (!path.empty() && path != it->second) all.members.emplace_back(name, json::Json::string(path));
			changed.set(json::write(all));
			return resolve(name);
		});
	command("resolve", "where a folder is, made absolute", {"name"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		const std::string r = resolve(name);
		if (r.empty()) error = "no folder called " + name;
		return r;
	});
	command("dialogFolder", "where a file dialog for this folder opens: the folder, or the nearest one above it that is "
		"there (nothing is made); for ROM, where the last game came from when recentForRoms", {"name"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			if (name == "ROM" && recentForRoms.get() && !lastRomFolder.get().empty() && fs::is_directory(lastRomFolder.get())) return lastRomFolder.get();
			const std::string r = resolve(name);
			if (r.empty())
			{
				error = "no folder called " + name;
				return {};
			}
			std::error_code ec;
			fs::path at(r);
			while (!at.empty() && !fs::is_directory(at, ec) && at.has_parent_path() && at.parent_path() != at) at = at.parent_path();
			return at.string();
		});
	command("resetAll", "every folder back to Chimera's", {}, [this](const Args &, std::string &) -> Value {
		changed.set("{}");
		base.set("");
		return {};
	});
	auto refresh = [this] {
		rows_.reset.emit();
		moved.emit();
	};
	baseWatch_ = base.changed.connect(refresh);
	programWatch_ = programFolder.changed.connect(refresh);
	changedWatch_ = changed.changed.connect(refresh);
}

std::string PathsViewModel::pathOf(const std::string &name) const
{
	const auto &d = defaults();
	const auto it = std::find_if(d.begin(), d.end(), [&](const auto &e) { return e.first == name; });
	if (it == d.end()) return {};
	json::Json all;
	std::string err;
	if (json::parse(changed.get(), all, err))
		if (const json::Json *v = all.find(name); v != nullptr && v->kind == json::Json::Kind::String && !v->text.empty()) return v->text;
	return it->second;
}

std::string PathsViewModel::resolve(const std::string &name) const
{
	const std::string path = pathOf(name);
	if (path.empty()) return {};
	const fs::path p(path);
	if (p.is_absolute()) return p.lexically_normal().string();
	/* Chimera's ResolveToolsPath: a relative Watch or Debug Logs folder is under Tools ("Tools" alone being Tools) */
	if (name == "Watch (.wch)" || name == "Debug Logs")
	{
		const std::string tools = resolve("Tools");
		std::string out = path == "Tools" ? tools : (fs::path(tools) / p).lexically_normal().string();
		while (out.size() > 1 && (out.back() == '/' || out.back() == '\\') && !(out.size() == 3 && out[1] == ':')) out.pop_back();
		return out;
	}
	const fs::path from = !base.get().empty() ? fs::path(base.get()) : fs::path(programFolder.get());
	std::string out = (from / p).lexically_normal().string();
	/* "." alone leaves a separator at the end */
	while (out.size() > 1 && (out.back() == '/' || out.back() == '\\') && !(out.size() == 3 && out[1] == ':')) out.pop_back();
	return out;
}

int64_t PathsViewModel::Rows::rowCount() const { return static_cast<int64_t>(defaults().size()); }

std::string PathsViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "path", "folder", "isDefault"};
	return column >= 0 && column < 4 ? names[column] : "";
}

Value PathsViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const auto &e = defaults()[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return e.first;
	case 1: return vm_.pathOf(e.first);
	case 2: return vm_.resolve(e.first);
	case 3: return vm_.pathOf(e.first) == e.second;
	default: return {};
	}
}

} // namespace chimera::vm
