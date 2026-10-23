#include "chimera/vm/cache_manager_vm.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace fs = std::filesystem;

CacheManagerViewModel::CacheManagerViewModel(Dispatcher &dispatcher) : ViewModel("cacheManager", dispatcher)
{
	expose("cacheFolder", "where the project caches are", cacheFolder, true);
	expose("total", "all of it together, as it reads", total);
	expose("inUse", "the open project's cache folder name, \"\" for none", inUse, true);
	rows("caches", "one row a project's cache: title, project, core, system, games, size (bytes), sizeText, orphan (its "
		"project is not where it was last seen), folder, open (the project open's), note", rows_);
	command("refresh", "looks at what is kept again", {}, [this](const Args &, std::string &) -> Value {
		survey();
		return static_cast<int64_t>(items_.size());
	});
	command("remove", "removes the caches at these places in the list (\"0,2\"); their projects replay instead of resuming",
		{"rows"}, [this](const Args &a, std::string &error) -> Value {
			std::string list;
			if (!arg(a, 0, list, error)) return {};
			std::vector<size_t> picked;
			std::stringstream in(list);
			for (std::string one; std::getline(in, one, ',');)
				if (!one.empty())
				{
					char *end = nullptr;
					const long i = std::strtol(one.c_str(), &end, 10);
					if (end == nullptr || *end != '\0' || i < 0 || static_cast<size_t>(i) >= items_.size())
					{
						error = "no row " + one;
						return {};
					}
					picked.push_back(static_cast<size_t>(i));
				}
			int64_t removed = 0;
			std::string failed;
			for (size_t i : picked)
			{
				std::error_code ec;
				/* only ever a folder inside the cache folder */
				const fs::path folder = items_[i].folder;
				if (folder.parent_path() != fs::path(cacheFolder.get())) continue;
				/* the open project's is being written */
				if (items_[i].open)
				{
					failed += (failed.empty() ? "" : ", ") + items_[i].title + " (open)";
					continue;
				}
				fs::remove_all(folder, ec);
				if (ec) failed += (failed.empty() ? "" : ", ") + items_[i].title;
				else removed++;
			}
			survey();
			if (!failed.empty()) error = "could not remove: " + failed;
			return removed;
		});
	command("orphans", "the places in the list of caches whose project is gone (\"1,3\")", {}, [this](const Args &, std::string &) -> Value {
		std::string list;
		for (size_t i = 0; i < items_.size(); i++)
			if (items_[i].orphan) list += (list.empty() ? "" : ",") + std::to_string(i);
		return list;
	});
	folderWatch_ = cacheFolder.changed.connect([this] { survey(); });
	inUseWatch_ = inUse.changed.connect([this] { survey(); });
}

std::string CacheManagerViewModel::sizeText(uint64_t bytes)
{
	const char *units[] = {"bytes", "KB", "MB", "GB", "TB"};
	double v = static_cast<double>(bytes);
	int u = 0;
	while (v >= 1024 && u < 4) v /= 1024, u++;
	char text[32];
	if (u == 0) std::snprintf(text, sizeof text, "%llu bytes", static_cast<unsigned long long>(bytes));
	else std::snprintf(text, sizeof text, "%.1f %s", v, units[u]);
	return text;
}

void CacheManagerViewModel::survey()
{
	items_.clear();
	std::error_code ec;
	const fs::path root = cacheFolder.get();
	uint64_t all = 0;
	if (!root.empty() && fs::is_directory(root, ec))
		for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_directory(ec)) continue;
			Item item;
			item.folder = it->path().string();
			item.title = "(a project that never said its name)";
			item.open = !inUse.get().empty() && it->path().filename().string() == inUse.get();
			/* who it belongs to, when it says */
			std::ifstream in(it->path() / "about.json", std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			json::Json about;
			std::string err;
			if (json::parse(text, about, err))
			{
				if (const json::Json *t = about.find("title"); t != nullptr && !t->text.empty()) item.title = t->text;
				if (const json::Json *p = about.find("projectPath")) item.projectPath = p->text;
				if (const json::Json *c = about.find("core")) item.core = c->text;
				if (const json::Json *s = about.find("system")) item.system = s->text;
				if (const json::Json *g = about.find("games"))
					for (const json::Json &one : g->items) item.games += (item.games.empty() ? "" : ", ") + one.text;
			}
			/* as Chimera: only a project whose place is known and empty now; the open one is not missing */
			item.orphan = !item.open && !item.projectPath.empty() && !fs::exists(item.projectPath, ec);
			for (fs::recursive_directory_iterator f(it->path(), ec), fend; !ec && f != fend; f.increment(ec))
				if (f->is_regular_file(ec)) item.bytes += f->file_size(ec);
			all += item.bytes;
			items_.push_back(std::move(item));
		}
	/* the largest first: what is worth reclaiming is at the top */
	std::stable_sort(items_.begin(), items_.end(), [](const Item &a, const Item &b) { return a.bytes > b.bytes; });
	total.set(sizeText(all) + " in " + std::to_string(items_.size()) + (items_.size() == 1 ? " project" : " projects"));
	rows_.reset.emit();
}

std::string CacheManagerViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"title", "project", "core", "system", "games", "size", "sizeText", "orphan", "folder", "open", "note"};
	return column >= 0 && column < 11 ? names[column] : "";
}

Value CacheManagerViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Item &i = vm_.items_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return i.title;
	case 1: return i.projectPath;
	case 2: return i.core;
	case 3: return i.system;
	case 4: return i.games;
	case 5: return static_cast<int64_t>(i.bytes);
	case 6: return sizeText(i.bytes);
	case 7: return i.orphan;
	case 8: return i.folder;
	case 9: return i.open;
	case 10:
		return i.open ? std::string("In use: the project open now.")
			   : i.orphan ? std::string("The project this belongs to is not where it was last seen.")
						  : std::string();
	default: return {};
	}
}

} // namespace chimera::vm
