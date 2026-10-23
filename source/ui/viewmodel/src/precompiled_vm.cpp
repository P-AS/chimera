#include "chimera/vm/precompiled_vm.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "chimera/vm/cache_manager_vm.hpp"
#include "chimera/vm/european_clock.hpp"
#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace fs = std::filesystem;

std::string PrecompiledViewModel::sizeText(uint64_t bytes)
{
	const double gb = static_cast<double>(bytes) / (1024.0 * 1024 * 1024);
	char buf[32];
	if (gb >= 1.0) std::snprintf(buf, sizeof buf, "%.2f GB", gb);
	else if (static_cast<double>(bytes) / (1024.0 * 1024) >= 1.0) std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024));
	else std::snprintf(buf, sizeof buf, "%.0f KB", std::floor(static_cast<double>(bytes) / 1024.0 + 0.5));
	return buf;
}

PrecompiledViewModel::PrecompiledViewModel(Dispatcher &dispatcher) : ViewModel("precompiled", dispatcher)
{
	expose("header", "what the window says over the list: how many games, how much, and what removing costs", header);
	command("sizeOf", "how much the games at these places in the list (\"0,2\") weigh, as Chimera writes it", {"rows"},
		[this](const Args &a, std::string &error) -> Value {
			std::string list;
			if (!arg(a, 0, list, error)) return {};
			uint64_t bytes = 0;
			std::stringstream in(list);
			for (std::string one; std::getline(in, one, ',');)
				if (!one.empty())
				{
					const long i = std::strtol(one.c_str(), nullptr, 10);
					if (i >= 0 && static_cast<size_t>(i) < games_.size()) bytes += games_[static_cast<size_t>(i)].bytes;
				}
			return sizeText(bytes);
		});
	expose("root", "where the compiled code is, a folder a game", root, true);
	expose("inUse", "the open machine's folder, \"\" for none: it stays", inUse, true);
	expose("total", "all of it together, as it reads", total);
	expose("status", "what the window last did", status);
	rows("games", "one row a game's folder: name (the game's file, else its SHA1), sha1, core, version, compiled (when), "
		"modules, size (bytes), sizeText, note, folder", rows_);
	command("refresh", "looks at the folder again", {}, [this](const Args &, std::string &) -> Value {
		survey();
		return static_cast<int64_t>(games_.size());
	});
	command("remove", "removes the games' folders at these places in the list (\"0,2\"); their code is compiled again "
		"when a project needs it", {"rows"}, [this](const Args &a, std::string &error) -> Value {
			std::string list;
			if (!arg(a, 0, list, error)) return {};
			std::vector<size_t> picked;
			std::stringstream in(list);
			for (std::string one; std::getline(in, one, ',');)
				if (!one.empty())
				{
					char *end = nullptr;
					const long i = std::strtol(one.c_str(), &end, 10);
					if (end == nullptr || *end != '\0' || i < 0 || static_cast<size_t>(i) >= games_.size())
					{
						error = "no row " + one;
						return {};
					}
					picked.push_back(static_cast<size_t>(i));
				}
			int64_t removed = 0;
			uint64_t freed = 0;
			std::string failed;
			for (size_t i : picked)
			{
				const Game &g = games_[i];
				/* only ever a folder inside the root, and not the open machine's */
				if (fs::path(g.folder).parent_path() != fs::path(root.get())) continue;
				if (g.open)
				{
					failed += (failed.empty() ? "" : ", ") + g.name + " (in use)";
					continue;
				}
				std::error_code ec;
				fs::remove_all(g.folder, ec);
				if (ec)
					failed += (failed.empty() ? "" : ", ") + g.name;
				else
				{
					removed++;
					freed += g.bytes;
				}
			}
			survey();
			status.set("Removed " + std::to_string(removed) + ", freeing " + CacheManagerViewModel::sizeText(freed) + ".");
			if (!failed.empty()) error = "could not remove: " + failed;
			return removed;
		});
	rootWatch_ = root.changed.connect([this] { survey(); });
	inUseWatch_ = inUse.changed.connect([this] { survey(); });
}

void PrecompiledViewModel::survey()
{
	games_.clear();
	std::error_code ec;
	uint64_t all = 0;
	if (!root.get().empty() && fs::is_directory(root.get(), ec))
		for (fs::directory_iterator it(root.get(), ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_directory(ec)) continue;
			Game g;
			g.folder = it->path().string();
			g.sha1 = it->path().filename().string();
			g.name = g.sha1;
			g.open = !inUse.get().empty() && fs::path(inUse.get()) == it->path();
			std::ifstream in(it->path() / "manifest.json", std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			json::Json m;
			std::string err;
			if (!text.empty() && json::parse(text, m, err) && m.kind == json::Json::Kind::Object)
			{
				g.unknown = false;
				if (const json::Json *v = m.find("rom"); v != nullptr && !v->text.empty()) g.name = v->text;
				if (const json::Json *v = m.find("core")) g.core = v->text;
				if (const json::Json *v = m.find("coreVersion")) g.version = v->text;
				if (const json::Json *v = m.find("compiled")) g.compiled = v->text;
				if (const json::Json *files = m.find("files"); files != nullptr && files->kind == json::Json::Kind::Array)
				{
					g.modules = static_cast<int64_t>(files->items.size());
					int64_t present = 0;
					for (const json::Json &f : files->items)
						if (const json::Json *n = f.find("name"); n != nullptr && fs::exists(it->path() / n->text, ec)) present++;
					g.complete = g.modules > 0 && present == g.modules;
				}
			}
			for (fs::recursive_directory_iterator f(it->path(), ec), fend; !ec && f != fend; f.increment(ec))
				if (f->is_regular_file(ec)) g.bytes += f->file_size(ec);
			ec.clear();
			all += g.bytes;
			games_.push_back(std::move(g));
		}
	/* newest first; what nothing dates goes last */
	std::stable_sort(games_.begin(), games_.end(), [](const Game &a, const Game &b) { return a.compiled > b.compiled; });
	total.set(CacheManagerViewModel::sizeText(all) + " in " + std::to_string(games_.size()) + (games_.size() == 1 ? " game" : " games"));
	header.set(games_.empty() ? "No game has compiled code yet. A core that translates its game's code fills this when a project is created."
		: std::to_string(games_.size()) + (games_.size() == 1 ? " game, " : " games, ") + sizeText(all) +
			". Removing one means its game is compiled again the next time a project needs it, which costs minutes and never work.");
	rows_.reset.emit();
}

std::string PrecompiledViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "sha1", "core", "version", "compiled", "modules", "size", "sizeText", "note", "folder",
		"by", "when", "modulesText", "sizeShown", "look"};
	return column >= 0 && column < 15 ? names[column] : "";
}

Value PrecompiledViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Game &g = vm_.games_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return g.name;
	case 1: return g.sha1;
	case 2: return g.core;
	case 3: return g.version;
	case 4: return g.compiled;
	case 5: return g.modules;
	case 6: return static_cast<int64_t>(g.bytes);
	case 7: return CacheManagerViewModel::sizeText(g.bytes);
	case 8:
		return g.open ? std::string("In use: the machine open now.")
			   : g.unknown ? std::string("Nothing here says which game or core this belongs to - most likely a compile at a boot, or one that was interrupted.")
			   : g.complete ? std::string()
							: std::string("Some of the objects this game needs are missing; it will be compiled again when a project needs it.");
	case 9: return g.folder;
	/* Chimera's columns: who compiled it, when (local time), the modules, the size, and how it reads */
	case 10:
		if (g.core.empty()) return std::string("not recorded");
		return g.version.empty() ? g.core : g.core + " " + g.version.substr(0, 12);
	case 11:
	{
		int64_t t = 0;
		if (g.compiled.empty() || !parseIsoTime(g.compiled, t)) return std::string();
		const std::time_t tt = static_cast<std::time_t>(t);
		std::tm local{};
#ifdef _WIN32
		localtime_s(&local, &tt);
#else
		localtime_r(&tt, &local);
#endif
		char buf[32];
		std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &local);
		return std::string(buf);
	}
	case 12: return g.modules == 0 ? std::string() : std::to_string(g.modules);
	case 13: return sizeText(g.bytes);
	/* nothing can name it: greyed; pieces missing: the error colour */
	case 14: return std::string(g.unknown ? "grey" : g.complete ? "normal" : "error");
	default: return {};
	}
}

} // namespace chimera::vm
