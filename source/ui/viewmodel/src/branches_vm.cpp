#include "chimera/vm/branches_vm.hpp"

#include <chrono>
#include <ctime>

namespace chimera::vm {

namespace {
std::string now()
{
	const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &t);
#else
	localtime_r(&t, &local);
#endif
	char buf[32];
	std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &local);
	return buf;
}
} // namespace

BranchesViewModel::BranchesViewModel(Dispatcher &dispatcher, InputLog &inputs, SessionViewModel &session, PianoRollViewModel &roll)
	: ViewModel("branches", dispatcher), inputs_(inputs), session_(session), roll_(roll)
{
	expose("count", "how many branches the movie has", count);
	expose("status", "what the branches last had to say", status);
	rows("branches", "one row a branch: name, frame, when it was made, how many frames it has", rows_);

	command("create", "keeps the movie as it is now, and the frame the machine is on, as a branch", {"name"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!a.empty() && !arg(a, 0, name, error)) return {};
			if (name.empty()) name = "Branch " + std::to_string(branches_.size() + 1);
			SessionViewModel::Branch b;
			b.name = name;
			b.time = now();
			b.frame = session_.frame.get();
			b.text = inputs_.text();
			b.frames = inputs_.size();
			for (const auto &[f, m] : roll_.markers()) b.markers.push_back(m);
			branches_.push_back(std::move(b));
			changed();
			status.set("Made " + name + " at frame " + std::to_string(session_.frame.get()));
			return static_cast<int64_t>(branches_.size() - 1);
		}, &session_.isOpen);
	command("load", "puts a branch's movie back, and the machine on its frame", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			if (!index(a, i, error)) return {};
			const SessionViewModel::Branch &b = branches_[i];
			/* the engine takes the branch's movie, keeps the states before
			 * the first frame where the two differ, and replays from there */
			session_.loadMovie(b.text, b.frame);
			roll_.replaceAll(b.markers);
			status.set("Loaded " + b.name);
			return {};
		}, &session_.isOpen);
	command("remove", "forgets a branch", {"index"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		if (!index(a, i, error)) return {};
		status.set("Removed " + branches_[i].name);
		branches_.erase(branches_.begin() + static_cast<long>(i));
		changed();
		return {};
	});
	command("rename", "names a branch again", {"index", "name"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		std::string name;
		if (!index(a, i, error) || !arg(a, 1, name, error)) return {};
		branches_[i].name = name;
		changed();
		return {};
	});

	loadedWatch_ = session_.branchesLoaded.connect([this](const std::vector<SessionViewModel::Branch> &loaded) {
		branches_ = loaded;
		count.set(static_cast<int64_t>(branches_.size()));
		rows_.reset.emit();
	});
}

bool BranchesViewModel::index(const Args &a, size_t &out, std::string &error) const
{
	int64_t i = 0;
	if (!arg(a, 0, i, error)) return false;
	if (i < 0 || i >= static_cast<int64_t>(branches_.size()))
	{
		error = "no branch " + std::to_string(i);
		return false;
	}
	out = static_cast<size_t>(i);
	return true;
}

void BranchesViewModel::changed()
{
	count.set(static_cast<int64_t>(branches_.size()));
	session_.setBranches(branches_);
	rows_.reset.emit();
}

std::string BranchesViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "frame", "time", "frames"};
	return column >= 0 && column < 4 ? names[column] : "";
}

Value BranchesViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const SessionViewModel::Branch &b = vm_.branches_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return b.name;
	case 1: return b.frame;
	case 2: return b.time;
	case 3: return b.frames;
	default: return {};
	}
}

} // namespace chimera::vm
