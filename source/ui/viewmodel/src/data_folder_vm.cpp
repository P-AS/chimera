#include "chimera/vm/data_folder_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace fs = std::filesystem;

namespace data_folder {

namespace {
/* what the window's data folder is recognised by: the names at its top */
const char *const kMarks[] = {"projects", "recovery"};

std::string withSeparator(const fs::path &p)
{
	std::string s = fs::absolute(p).lexically_normal().string();
	while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
	/* the separator lexically_normal wrote: "/" after a Windows path was
	 * never a prefix of the folders inside it */
	return s + static_cast<char>(fs::path::preferred_separator);
}

uint64_t sizeOf(const fs::path &p)
{
	std::error_code ec;
	if (fs::is_regular_file(p, ec)) return fs::file_size(p, ec);
	uint64_t total = 0;
	for (fs::recursive_directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec))
		if (it->is_regular_file(ec)) total += it->file_size(ec);
	return total;
}

void copyTree(const fs::path &from, const fs::path &to)
{
	if (fs::is_regular_file(from))
	{
		fs::copy_file(from, to);
		fs::last_write_time(to, fs::last_write_time(from));
		return;
	}
	fs::create_directories(to);
	for (fs::recursive_directory_iterator it(from), end; it != end; ++it)
	{
		const fs::path dest = to / fs::relative(it->path(), from);
		if (it->is_directory())
			fs::create_directories(dest);
		else if (it->is_regular_file())
		{
			fs::copy_file(it->path(), dest);
			/* the Cache Manager reads ages from these */
			fs::last_write_time(dest, fs::last_write_time(it->path()));
		}
	}
}
} // namespace

bool holdsData(const std::string &dir)
{
	std::error_code ec;
	if (!fs::is_directory(dir, ec)) return false;
	return std::any_of(std::begin(kMarks), std::end(kMarks), [&](const char *m) { return fs::exists(fs::path(dir) / m, ec); });
}

std::string targetFor(const std::string &picked)
{
	std::error_code ec;
	const fs::path full = fs::absolute(picked, ec).lexically_normal();
	if (!fs::is_directory(full, ec) || holdsData(full.string())) return full.string();
	return fs::directory_iterator(full, ec) != fs::directory_iterator() ? (full / "chimera").string() : full.string();
}

Choice inspect(const std::string &current, const std::string &target)
{
	std::string from = withSeparator(current), to = withSeparator(target);
#ifdef _WIN32
	/* D:\Data and d:\data are one folder on Windows */
	for (std::string *t : {&from, &to})
		for (char &c : *t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
	if (from == to) return Choice::Same;
	if (to.rfind(from, 0) == 0 || from.rfind(to, 0) == 0) return Choice::Nested;
	/* asked about, not made: whether it can be written is tried where it
	 * would be made, in the nearest folder there is, and nothing is left */
	std::error_code ec;
	fs::path there = fs::path(target);
	while (!there.empty() && !fs::exists(there, ec) && there.has_parent_path() && there.parent_path() != there) there = there.parent_path();
	const fs::path probe = there / ".chimera-write-test";
	{
		std::ofstream out(probe, std::ios::binary);
		if (!out) return Choice::Unwritable;
	}
	fs::remove(probe, ec);
	return holdsData(target) ? Choice::HoldsData : Choice::Empty;
}

std::string move(const std::string &from, const std::string &to)
{
	std::error_code ec;
	if (!fs::is_directory(from, ec)) return {}; /* nothing was ever kept */
	struct Done
	{
		fs::path from, to;
		bool copied;
	};
	std::vector<Done> done;
	try
	{
		fs::create_directories(to);
		std::vector<fs::path> entries;
		for (const auto &e : fs::directory_iterator(from)) entries.push_back(e.path());
		for (const fs::path &e : entries)
			if (fs::exists(fs::path(to) / e.filename())) throw std::runtime_error(tr("data_folder.already_there", {(fs::path(to) / e.filename()).string()}));
		for (const fs::path &e : entries)
		{
			const fs::path dest = fs::path(to) / e.filename();
			const uint64_t size = sizeOf(e);
			std::error_code rename;
			/* the same volume: no bytes move */
			fs::rename(e, dest, rename);
			if (!rename)
			{
				done.push_back({e, dest, false});
				continue;
			}
			done.push_back({e, dest, true});
			copyTree(e, dest);
			if (sizeOf(dest) != size) throw std::runtime_error(tr("data_folder.wrong_size", {dest.string(), e.string()}));
		}
		/* every byte is in the new place: only now does the old go */
		for (const Done &d : done)
			if (d.copied) fs::remove_all(d.from, ec);
		if (fs::is_empty(from, ec)) fs::remove(from, ec);
		return {};
	}
	catch (const std::exception &failed)
	{
		for (auto it = done.rbegin(); it != done.rend(); ++it)
		{
			std::error_code undo;
			if (it->copied)
				fs::remove_all(it->to, undo);
			else
				fs::rename(it->to, it->from, undo);
		}
		return failed.what();
	}
}

int64_t sizeOf(const std::string &dir)
{
	std::error_code ec;
	int64_t total = 0;
	if (!fs::is_directory(dir, ec)) return 0;
	for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
	{
		if (ec) break;
		if (it->is_regular_file(ec)) total += static_cast<int64_t>(it->file_size(ec));
	}
	return total;
}

int64_t freeAt(const std::string &dir)
{
	std::error_code ec;
	fs::path at(dir);
	/* a folder not made yet is on the drive of the nearest one above it that is */
	while (!at.empty() && !fs::exists(at, ec) && at.has_parent_path() && at.parent_path() != at) at = at.parent_path();
	const fs::space_info s = fs::space(at, ec);
	return ec ? 0 : static_cast<int64_t>(s.available);
}

std::string bytesText(int64_t bytes)
{
	char buf[64];
	const bool gb = bytes >= (int64_t{1} << 30), mb = bytes >= (int64_t{1} << 20);
	if (gb) std::snprintf(buf, sizeof buf, "%.1f", static_cast<double>(bytes) / static_cast<double>(int64_t{1} << 30));
	else if (mb) std::snprintf(buf, sizeof buf, "%.1f", static_cast<double>(bytes) / static_cast<double>(int64_t{1} << 20));
	else std::snprintf(buf, sizeof buf, "%.0f", static_cast<double>(bytes) / 1024.0);
	return tr(gb ? "units.gb" : mb ? "units.mb" : "units.kb", {buf});
}

} // namespace data_folder

DataFolderViewModel::DataFolderViewModel(Dispatcher &dispatcher) : ViewModel("dataFolder", dispatcher)
{
	expose("current", "the data folder in use; \"\" the system's place (standard)", current, true);
	expose("pending", "the data folder asked for, for the next start; \"\" none", pending, true);
	expose("pendingMove", "what is in the data folder moves with the change", pendingMove, true);
	expose("standard", "the system's place for the data folder", standard);
	expose("environment", "CHIMERA_DATA_HOME when it is set: it decides the data folder, and the window changes nothing", environment);
	expose("status", "what the data folder last had to say", status);
	expose("figures", "how much is in the folder in use and how much its drive has free (refresh)", figures);
	command("refresh", "says again how much is in the folder in use and how much its drive has free", {},
		[this](const Args &, std::string &) -> Value {
				figures.set(tr("data_folder.figures", {data_folder::bytesText(data_folder::sizeOf(inUse())), data_folder::bytesText(data_folder::freeAt(inUse()))}));
			return {};
		});
	command("inspect", "what a folder picked would be, before anything is asked: {\"target\", \"choice\" (same, nested, "
		"unwritable, holdsData, empty), \"size\" and \"free\" (bytes to move and free there, as Chimera writes them)}",
		{"folder"}, [this](const Args &a, std::string &error) -> Value {
			std::string folder;
			if (!arg(a, 0, folder, error)) return {};
			const std::string target = folder.empty() ? standard.get() : data_folder::targetFor(folder);
			const data_folder::Choice c = data_folder::inspect(inUse(), target);
			const char *word = c == data_folder::Choice::Same ? "same" : c == data_folder::Choice::Nested ? "nested"
				: c == data_folder::Choice::Unwritable ? "unwritable" : c == data_folder::Choice::HoldsData ? "holdsData" : "empty";
			const int64_t size = data_folder::sizeOf(inUse()), free = data_folder::freeAt(target);
			json::Json o;
			o.kind = json::Json::Kind::Object;
			o.members.emplace_back("target", json::Json::string(target));
			o.members.emplace_back("choice", json::Json::string(word));
			o.members.emplace_back("size", json::Json::string(data_folder::bytesText(size)));
			o.members.emplace_back("free", json::Json::string(data_folder::bytesText(free)));
			o.members.emplace_back("tight", json::Json::boolean_(free > 0 && free < size));
			return json::write(o);
		});
	command("inUse", "the data folder in use, made absolute", {}, [this](const Args &, std::string &) -> Value { return inUse(); });
	command("request", "asks for the data folder to become this one at the next start (\"\" the system's place), moving "
		"what is there when asked; says what the folder is", {"folder", "move"}, [this](const Args &a, std::string &error) -> Value {
			std::string folder;
			bool carry = true;
			if (!arg(a, 0, folder, error)) return {};
			if (a.size() > 1 && !arg(a, 1, carry, error)) return {};
			if (!environment.get().empty())
			{
				error = tr("data_folder.env_decides");
				return {};
			}
			const std::string target = folder.empty() ? standard.get() : data_folder::targetFor(folder);
			switch (data_folder::inspect(inUse(), target))
			{
			case data_folder::Choice::Same:
				pending.set("");
				pendingMove.set(false);
				status.set(tr("data_folder.already_here"));
				return std::string("same");
			case data_folder::Choice::Nested:
				error = tr("data_folder.inside_itself", {target});
				return {};
			case data_folder::Choice::Unwritable:
				error = tr("data_folder.not_writable", {target});
				return {};
			case data_folder::Choice::HoldsData:
			case data_folder::Choice::Empty: break;
			}
			pending.set(target);
			pendingMove.set(carry);
			status.set(tr("data_folder.recorded"));
			return std::string(data_folder::holdsData(target) ? "holdsData" : "empty");
		});
	command("withdraw", "the change asked for, forgotten", {}, [this](const Args &, std::string &) -> Value {
		pending.set("");
		pendingMove.set(false);
		status.set(tr("data_folder.withdrawn"));
		return {};
	});
}

std::string DataFolderViewModel::inUse() const
{
	if (!environment.get().empty()) return environment.get();
	return current.get().empty() ? standard.get() : current.get();
}

std::string DataFolderViewModel::applyPending()
{
	/* the environment decides: a change asked for waits until it does not */
	if (!environment.get().empty()) return {};
	const std::string to = pending.get();
	if (to.empty()) return {};
	const bool carry = pendingMove.get();
	pending.set("");
	pendingMove.set(false);
	const std::string from = inUse();
	const data_folder::Choice c = data_folder::inspect(from, to);
	const std::string chosen = to == standard.get() ? std::string() : to;
	if (c == data_folder::Choice::Same)
	{
		current.set(chosen);
		return {};
	}
	if (c == data_folder::Choice::Nested || c == data_folder::Choice::Unwritable)
		return tr("data_folder.not_changed", {to, from});
	if (carry)
		if (const std::string why = data_folder::move(from, to); !why.empty())
			return tr("data_folder.not_moved", {why, from});
	current.set(chosen);
	return {};
}

} // namespace chimera::vm
