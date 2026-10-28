#include "chimera/vm/suggestion.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

#include "chimera/engine.h"

namespace chimera::vm {

using json::Json;

std::vector<AskFile> askFilesOf(const std::string &package, const std::string &game, const Mounts &files, const std::string &slotsJson)
{
	std::vector<AskFile> out;
	Json slots;
	std::string error;
	if (json::parse(slotsJson, slots, error) && slots.kind == Json::Kind::Object)
		for (const auto &[slot, names] : slots.members)
			for (const Json &n : names.items)
				for (const auto &[name, path] : files)
					if (name == n.text)
					{
						out.push_back({slot, name, path});
						break;
					}
	if (game.empty() || std::any_of(out.begin(), out.end(), [&](const AskFile &f) { return f.path == game; })) return out;
	/* the game alone: the slot that takes its kind of file, else the first */
	std::string kind = std::filesystem::path(game).extension().string();
	if (!kind.empty()) kind.erase(0, 1);
	std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	std::string slot = "rom";
	if (ce_package *pk = ce_package_open(package.c_str(), nullptr))
	{
		uint64_t len = 0;
		Json decl;
		if (const uint8_t *d = ce_package_entry_texts(pk, "file_slots.json", &len);
			d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), decl, error))
			if (const Json *list = decl.find("slots"); list != nullptr && !list->items.empty())
			{
				slot = list->items.front().find("id") != nullptr ? list->items.front().find("id")->text : slot;
				for (const Json &s : list->items)
					if (const Json *formats = s.find("formats"))
						for (const Json &f : formats->items)
						{
							std::string format = f.text;
							std::transform(format.begin(), format.end(), format.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
							if (format == kind && s.find("id") != nullptr) slot = s.find("id")->text;
						}
			}
		ce_package_free(pk);
	}
	out.push_back({slot, std::filesystem::path(game).filename().string(), game});
	return out;
}

ce_project *askingProject(const std::string &package, const std::vector<AskFile> &files, const std::string &settingsJson, std::string &why)
{
	/* the package's core by name, not pinned: the package given is the one asked */
	std::string coreName, version;
	const char *error = nullptr;
	ce_package *pk = ce_package_open(package.c_str(), &error);
	if (pk == nullptr)
	{
		why = error != nullptr ? error : tr("suggestion.not_package", {package});
		return nullptr;
	}
	{
		uint64_t len = 0;
		Json cfg;
		std::string parseError;
		if (const uint8_t *d = ce_package_entry_texts(pk, "waterbox.config", &len);
			d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, parseError))
		{
			if (const Json *n = cfg.find("coreName")) coreName = n->text;
			if (const Json *v = cfg.find("version")) version = v->text;
		}
		ce_package_free(pk);
	}
	ce_project *p = ce_project_new();
	ce_project_set_core(p, coreName.c_str(), version.c_str(), "");
	for (const AskFile &f : files)
	{
		if (f.slot.empty()) continue;
		if (ce_project_file_add(p, f.name.c_str(), f.slot.c_str(), f.path.c_str(), &error) != 0)
		{
			why = tr("session.named_reason", {f.name, error != nullptr ? error : tr("suggestion.not_taken")});
			ce_project_free(p);
			return nullptr;
		}
	}
	if (!settingsJson.empty() && settingsJson != "{}" && ce_project_set_settings_text(p, settingsJson.c_str(), &error) != 0)
	{
		why = tr("suggestion.settings", {error != nullptr ? error : tr("suggestion.not_taken")});
		ce_project_free(p);
		return nullptr;
	}
	return p;
}

bool saveAskingProject(const std::string &package, const std::vector<AskFile> &files, const std::string &settingsJson,
	const std::string &path, std::vector<std::string> &dirs, std::string &why)
{
	ce_project *p = askingProject(package, files, settingsJson, why);
	if (p == nullptr) return false;
	const char *error = nullptr;
	const bool saved = ce_project_save(p, path.c_str(), &error) == 0;
	ce_project_free(p);
	if (!saved)
	{
		why = error != nullptr ? error : tr("suggestion.not_written");
		return false;
	}
	for (const AskFile &f : files)
	{
		if (f.slot.empty()) continue;
		const std::string dir = std::filesystem::path(f.path).parent_path().string();
		if (std::find(dirs.begin(), dirs.end(), dir) == dirs.end()) dirs.push_back(dir);
	}
	return true;
}

std::string askForSuggestion(const std::string &package, const std::string &game, const Mounts &files,
	const std::string &slotsJson, const std::string &settingsJson, std::string &why)
{
	ce_project *project = askingProject(package, askFilesOf(package, game, files, slotsJson), settingsJson, why);
	if (project == nullptr) return {};
	uint64_t len = 0;
	const char *error = nullptr;
	const char *answer = ce_project_suggest_settings(package.c_str(), project, nullptr, nullptr, nullptr, 0, &len, &error);
	ce_project_free(project);
	if (answer == nullptr)
	{
		why = error != nullptr ? error : tr("suggestion.not_asked");
		return {};
	}
	return std::string(answer, static_cast<size_t>(len));
}

std::vector<Json> gameSettingsOf(const Json &answer, const Json *packageSettings)
{
	std::vector<Json> out;
	const Json *list = answer.find("settings");
	if (list == nullptr || list->kind != Json::Kind::Array) return out;
	std::set<std::string> taken;
	if (packageSettings != nullptr)
		for (const Json &d : packageSettings->items)
			if (const Json *n = d.find("name")) taken.insert(n->text);
	for (const Json &d : list->items)
	{
		const Json *n = d.kind == Json::Kind::Object ? d.find("name") : nullptr;
		if (n == nullptr || n->kind != Json::Kind::String || n->text.empty() || !taken.insert(n->text).second) continue;
		out.push_back(d);
	}
	return out;
}

Json gameFirmwareOf(const Json &answer, const Json *packageFirmware)
{
	Json out;
	out.kind = Json::Kind::Array;
	const Json *list = answer.find("firmware");
	if (list == nullptr || list->kind != Json::Kind::Array) return out;
	std::set<std::string> taken;
	if (packageFirmware != nullptr)
		for (const Json &f : packageFirmware->items)
			if (const Json *i = f.find("id")) taken.insert(i->text);
	for (const Json &f : list->items)
	{
		const Json *i = f.kind == Json::Kind::Object ? f.find("id") : nullptr;
		if (i == nullptr || i->kind != Json::Kind::String || i->text.empty() || !taken.insert(i->text).second) continue;
		out.items.push_back(f);
	}
	return out;
}

} // namespace chimera::vm
