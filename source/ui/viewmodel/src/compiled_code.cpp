#include "chimera/vm/compiled_code.hpp"

#include "chimera/vm/json.hpp"
#include "chimera/engine.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>

namespace chimera::vm::compiled {

namespace fs = std::filesystem;

std::optional<Build> buildOf(const std::string &package)
{
	const char *perr = nullptr;
	ce_package *pk = ce_package_open(package.c_str(), &perr);
	if (pk == nullptr) return std::nullopt;
	std::optional<Build> out;
	uint64_t len = 0;
	json::Json cfg;
	std::string err;
	if (const uint8_t *d = ce_package_entry_texts(pk, "waterbox.config", &len);
		d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, err))
	{
		Build b;
		if (const json::Json *v = cfg.find("coreName")) b.core = v->text;
		if (const json::Json *v = cfg.find("version")) b.version = v->text;
		out = b;
	}
	ce_package_free(pk);
	return out;
}

std::optional<Build> ownerOf(const std::string &dir)
{
	std::ifstream in(fs::path(dir) / "manifest.json", std::ios::binary);
	if (!in) return std::nullopt;
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	json::Json m;
	std::string err;
	if (!json::parse(text, m, err) || m.kind != json::Json::Kind::Object) return std::nullopt;
	Build b;
	if (const json::Json *v = m.find("core")) b.core = v->text;
	if (const json::Json *v = m.find("coreVersion")) b.version = v->text;
	return b;
}

bool belongsTo(const std::string &dir, const Build &build)
{
	if (dir.empty()) return true;
	std::error_code ec;
	if (fs::is_empty(dir, ec) || !fs::exists(dir, ec)) return true;
	const std::optional<Build> owner = ownerOf(dir);
	return owner.has_value() && *owner == build;
}

bool claim(const std::string &dir, const Build &build)
{
	/* "" is no folder: never the current one emptied */
	if (dir.empty()) return false;
	std::error_code ec;
	bool removed = false;
	if (!belongsTo(dir, build))
	{
		for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
			fs::remove_all(it->path(), ec);
		removed = true;
	}
	fs::create_directories(dir, ec);
	if (!fs::exists(fs::path(dir) / "manifest.json", ec))
	{
		json::Json manifest;
		manifest.kind = json::Json::Kind::Object;
		manifest.members.emplace_back("core", json::Json::string(build.core));
		manifest.members.emplace_back("coreVersion", json::Json::string(build.version));
		json::Json files;
		files.kind = json::Json::Kind::Array;
		manifest.members.emplace_back("files", std::move(files));
		std::ofstream(fs::path(dir) / "manifest.json", std::ios::binary) << json::write(manifest);
	}
	return removed;
}

} // namespace chimera::vm::compiled
