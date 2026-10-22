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
	expose("made", "branches the machine has made, their states kept or not", made);
	expose("loads", "branches loaded", loads);
	expose("loadedFromState", "whether the last branch loaded came from its state, not a replay", loadedFromState);
	rows("branches", "one row a branch: name, frame, when it was made, how many frames it has", rows_);

	command("create", "keeps the movie as it is now, and the frame the machine is on, as a branch", {"name"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!a.empty() && !arg(a, 0, name, error)) return {};
			return keepRun(-1, name);
		}, &session_.isOpen);
	command("saveSlot", "puts the run as it stands into the branch at this place, over what it held (a new branch "
		"when there is none there): Chimera's numbered branch slots", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = 0;
			if (!arg(a, 0, i, error)) return {};
			if (i < 0)
			{
				error = "no branch " + std::to_string(i);
				return {};
			}
			return keepRun(i, "");
		}, &session_.isOpen);
	command("load", "puts a branch's movie back, and the machine on its frame", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			if (!index(a, i, error)) return {};
			const SessionViewModel::Branch &b = branches_[i];
			/* its state when it has one, else the engine takes the branch's
			 * movie, keeps the states before the first frame where the two
			 * differ, and replays from there */
			status.set("Loading " + b.name + "...");
			session_.loadBranch(b, [this, name = b.name](bool fromState, const std::string &why) {
				status.set("Loaded " + name + (fromState ? " from its state" : ", replayed to") +
					(why.empty() ? "" : " (its state could not be loaded: " + why + ")"));
				loadedFromState.set(fromState);
				loads.set(loads.get() + 1);
			});
			roll_.replaceAll(b.markers);
			return {};
		}, &session_.isOpen);
	command("remove", "forgets a branch", {"index"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		if (!index(a, i, error)) return {};
		status.set("Removed " + branches_[i].name);
		const SessionViewModel::Branch gone = branches_[i];
		branches_.erase(branches_.begin() + static_cast<long>(i));
		changed();
		session_.forgetBranch(gone);
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
		for (SessionViewModel::Branch &b : branches_) b.id = ++lastId_;
		count.set(static_cast<int64_t>(branches_.size()));
		rows_.reset.emit();
	});
}

int64_t BranchesViewModel::keepRun(int64_t slot, std::string name)
{
	/* listed at once; the machine says its frame and movie, and keeps its
	 * state, when it gets to it */
	SessionViewModel::Branch b;
	b.time = now();
	b.frame = session_.frame.get();
	b.text = inputs_.text();
	b.frames = inputs_.size();
	b.id = ++lastId_;
	for (const auto &[f, m] : roll_.markers()) b.markers.push_back(m);
	int64_t at = 0;
	if (slot >= 0 && slot < static_cast<int64_t>(branches_.size()))
	{
		/* the slot keeps its name; what it held goes, its state file too */
		const SessionViewModel::Branch old = branches_[static_cast<size_t>(slot)];
		b.name = name.empty() ? old.name : name;
		branches_[static_cast<size_t>(slot)] = b;
		at = slot;
		changed();
		session_.forgetBranch(old);
	}
	else
	{
		b.name = name.empty() ? "Branch " + std::to_string(branches_.size() + 1) : name;
		branches_.push_back(b);
		at = static_cast<int64_t>(branches_.size()) - 1;
		changed();
	}
	status.set("Making " + b.name + "...");
	session_.makeBranch(b.name, b.time, [this, id = b.id](SessionViewModel::Branch done, const std::string &why) {
		for (SessionViewModel::Branch &x : branches_)
			if (x.id == id)
			{
				x.frame = done.frame;
				x.text = std::move(done.text);
				x.frames = done.frames;
				x.state = done.state;
				x.picture = done.picture;
				changed();
				status.set("Made " + x.name + " at frame " + std::to_string(x.frame) +
					(!why.empty() ? "; its state could not be kept (" + why + "), so loading it replays"
					 : x.state.empty() ? "" : ", its state kept"));
				break;
			}
		made.set(made.get() + 1);
	});
	return at;
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
