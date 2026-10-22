#include "chimera/vm/cores_vm.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include "chimera/engine.h"
#include "chimera/vm/json.hpp"

namespace chimera::vm {

CoresViewModel::CoresViewModel(Dispatcher &dispatcher) : ViewModel("cores", dispatcher)
{
	expose("coresFolder", "the folder core packages are looked for in", coresFolder, true);
	expose("firmwareFolder", "the folder firmware is looked for in", firmwareFolder, true);
	expose("count", "packages found", count);
	expose("status", "what the last look at the folder found", status);
	rows("cores", "one row a package: core, version, when it was made, file", rows_);
	command("refresh", "looks at the cores folder again", {}, [this](const Args &, std::string &) -> Value {
		scan();
		return count.get();
	});
	/* the environment names them where a test or a portable install means it */
	if (const char *env = std::getenv("CHIMERA_CORES_DIR")) coresFolder.set(env);
	if (const char *env = std::getenv("CHIMERA_FIRMWARE_DIR")) firmwareFolder.set(env);
	folderWatch_ = coresFolder.changed.connect([this] { scan(); });
	firmwareWatch_ = firmwareFolder.changed.connect([this] {
		std::lock_guard<std::mutex> lock(m_);
		firmwareM_ = firmwareFolder.get();
	});
	firmwareM_ = firmwareFolder.get();
	scan();
}

void CoresViewModel::scan()
{
	namespace fs = std::filesystem;
	std::vector<Core> found;
	const std::string folder = coresFolder.get();
	std::error_code ec;
	if (!folder.empty() && fs::is_directory(folder, ec))
		for (const auto &entry : fs::directory_iterator(folder, ec))
		{
			if (entry.path().extension() != ".chimeraCore") continue;
			const std::string path = entry.path().string();
			const char *perr = nullptr;
			ce_package *p = ce_package_open(path.c_str(), &perr);
			if (p == nullptr) continue;
			uint64_t len = 0;
			const uint8_t *d = ce_package_entry(p, "waterbox.config", &len);
			json::Json cfg;
			std::string error;
			if (d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, error))
			{
				Core c;
				if (const json::Json *n = cfg.find("coreName")) c.coreName = n->text;
				if (const json::Json *v = cfg.find("version")) c.version = v->text;
				if (const json::Json *m = cfg.find("versionDate")) c.made = m->text;
				c.path = path;
				c.modified = static_cast<long long>(fs::last_write_time(entry.path(), ec).time_since_epoch().count());
				if (!c.coreName.empty()) found.push_back(std::move(c));
			}
			ce_package_free(p);
		}
	std::sort(found.begin(), found.end(), [](const Core &a, const Core &b) {
		return a.coreName != b.coreName ? a.coreName < b.coreName : a.modified > b.modified;
	});
	const int64_t n = static_cast<int64_t>(found.size());
	{
		std::lock_guard<std::mutex> lock(m_);
		cores_ = std::move(found);
	}
	count.set(n);
	status.set(folder.empty() ? "No cores folder" : std::to_string(n) + (n == 1 ? " package in " : " packages in ") + folder);
	rows_.reset.emit();
}

std::string CoresViewModel::packageFor(const std::string &coreName, const std::string &pinnedSha1, std::string &note) const
{
	std::vector<Core> builds;
	{
		std::lock_guard<std::mutex> lock(m_);
		for (const Core &c : cores_)
			if (c.coreName == coreName) builds.push_back(c);
	}
	if (builds.empty()) return {};
	/* the pinned one, when it is here: only then is its hash worth computing */
	if (!pinnedSha1.empty())
		for (const Core &c : builds)
		{
			const char *perr = nullptr;
			ce_package *p = ce_package_open(c.path.c_str(), &perr);
			if (p == nullptr) continue;
			const char *sha = ce_package_sha1(p);
			const bool match = sha != nullptr && pinnedSha1 == sha;
			ce_package_free(p);
			if (match) return c.path;
		}
	/* else the most recently installed (they are sorted newest first) */
	if (!pinnedSha1.empty()) note = "the package the project was made with is not here; the newest " + coreName + " is";
	return builds.front().path;
}

std::vector<std::string> CoresViewModel::firmwareFolders() const
{
	std::lock_guard<std::mutex> lock(m_);
	return firmwareM_.empty() ? std::vector<std::string>{} : std::vector<std::string>{firmwareM_};
}

int64_t CoresViewModel::Rows::rowCount() const
{
	std::lock_guard<std::mutex> lock(vm_.m_);
	return static_cast<int64_t>(vm_.cores_.size());
}

std::string CoresViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"core", "version", "made", "file"};
	return column >= 0 && column < 4 ? names[column] : "";
}

Value CoresViewModel::Rows::cell(int64_t row, int32_t column) const
{
	std::lock_guard<std::mutex> lock(vm_.m_);
	if (row < 0 || row >= static_cast<int64_t>(vm_.cores_.size())) return {};
	const Core &c = vm_.cores_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return c.coreName;
	case 1: return c.version.substr(0, 12);
	case 2: return c.made;
	case 3: return std::filesystem::path(c.path).filename().string();
	default: return {};
	}
}

} // namespace chimera::vm
