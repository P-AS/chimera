#include "chimera/vm/suggestion.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

#include "chimera/engine.h"

namespace chimera::vm {

using json::Json;

namespace {
bool isCue(const std::string &path)
{
	if (path.size() < 4) return false;
	std::string ext = path.substr(path.size() - 4);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext == ".cue";
}
} // namespace

Mounts withCueTracks(const std::string &game, Mounts files)
{
	namespace fs = std::filesystem;
	std::vector<std::string> cues;
	if (isCue(game)) cues.push_back(game);
	for (const auto &[name, path] : files)
		if (isCue(path) && std::find(cues.begin(), cues.end(), path) == cues.end()) cues.push_back(path);
	for (const std::string &cue : cues)
	{
		std::ifstream in(cue, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		uint64_t len = 0;
		const char *refs = ce_cue_references(text.data(), text.size(), &len);
		Json names;
		std::string error;
		if (refs == nullptr || !json::parse(std::string_view(refs, static_cast<size_t>(len)), names, error)) continue;
		for (const Json &n : names.items)
		{
			const bool listed = std::any_of(files.begin(), files.end(), [&](const auto &f) { return f.first == n.text; });
			const fs::path track = fs::path(cue).parent_path() / n.text;
			std::error_code ec;
			if (!listed && fs::is_regular_file(track, ec)) files.emplace_back(n.text, track.string());
		}
	}
	return files;
}

std::string askForSuggestion(const std::string &package, const std::string &game, const Mounts &files,
	const std::string &slotsJson, const std::string &settingsJson, std::string &why)
{
	std::vector<const char *> names, paths;
	std::vector<const uint8_t *> data;
	std::vector<uint64_t> lens;
	names.push_back("slots");
	paths.push_back(nullptr);
	data.push_back(reinterpret_cast<const uint8_t *>(slotsJson.data()));
	lens.push_back(slotsJson.size());
	for (const auto &[name, path] : files)
	{
		names.push_back(name.c_str());
		paths.push_back(path.c_str());
		data.push_back(nullptr);
		lens.push_back(0);
	}
	uint64_t len = 0;
	const char *error = nullptr;
	const char *answer = ce_suggest_settings(package.c_str(), nullptr, 0, game.c_str(), settingsJson.c_str(), nullptr, nullptr,
		nullptr, 0, names.data(), data.data(), lens.data(), paths.data(), static_cast<int32_t>(names.size()), &len, &error);
	if (answer == nullptr)
	{
		why = error != nullptr ? error : "the core could not be asked";
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
