#include "chimera/vm/piano_roll_vm.hpp"

#include <algorithm>

namespace chimera::vm {

PianoRollViewModel::PianoRollViewModel(Dispatcher &dispatcher, InputLog &inputs, SessionViewModel &session)
	: ViewModel("pianoRoll", dispatcher), inputs_(inputs), session_(session)
{
	expose("currentFrame", "the frame the machine is about to run", currentFrame);
	expose("frameCount", "how many frames have inputs", frameCount);
	expose("followPlayback", "keep the current frame in view while the machine runs", followPlayback, true);
	expose("greenEnd", "frames before this one were run with the inputs they have now", greenEnd);
	expose("topFrame", "the first frame in view", topFrame, true);
	expose("visibleFrames", "how many frames the view shows at once (the view sets it)", visibleFrames, true);
	rows("frames", "one row a frame, one column a button; a cell is true where the button is held", frames_);
	rows("markers", "the movie's markers: frame, text, whether the frame's state is kept", markerRows_);
	expose("markerCount", "how many markers the movie has", markerCount);
	expose("canUndo", "an edit can be taken back", canUndo);
	expose("canRedo", "an edit taken back can be done again", canRedo);

	command("addMarker", "puts a marker on a frame (or renames the one there)", {"frame", "text"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			std::string text;
			if (!arg(a, 0, f, error)) return {};
			if (a.size() > 1 && !arg(a, 1, text, error)) return {};
			if (f < 0)
			{
				error = "no frame before 0";
				return {};
			}
			markers_[f] = Marker{f, text, true};
			markersEdited(f);
			return {};
		});
	command("removeMarker", "takes the marker off a frame", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			if (!arg(a, 0, f, error)) return {};
			if (markers_.erase(f) == 0)
			{
				error = "no marker on frame " + std::to_string(f);
				return {};
			}
			markersEdited(f);
			return {};
		});
	command("nextMarker", "the first marker after a frame, or -1", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			if (!arg(a, 0, f, error)) return {};
			const auto it = markers_.upper_bound(f);
			return it == markers_.end() ? int64_t{-1} : it->first;
		});
	command("previousMarker", "the last marker before a frame, or -1", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			if (!arg(a, 0, f, error)) return {};
			auto it = markers_.lower_bound(f);
			if (it == markers_.begin()) return int64_t{-1};
			return (--it)->first;
		});

	command("toggle", "flips one button on one frame", {"frame", "button"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0, b = 0;
			if (!arg(a, 0, f, error) || !arg(a, 1, b, error)) return {};
			if (!paint(f, f, b, std::nullopt, false, error)) return {};
			return (inputs_.at(f) >> b & 1) != 0;
		});
	command("press", "a press on a cell: flips it, and a drag from it gives the cells it passes the same state",
		{"frame", "button"}, [this](const Args &a, std::string &error) -> Value {
			int64_t f = 0, b = 0;
			if (!arg(a, 0, f, error) || !arg(a, 1, b, error)) return {};
			if (!paint(f, f, b, std::nullopt, false, error)) return {};
			dragHeld_ = (inputs_.at(f) >> b & 1) != 0;
			/* the drag that follows is the same edit: one undo takes it all back */
			stroke_ = true;
			return dragHeld_;
		});
	command("drag", "a drag from a pressed cell reached this one: it takes the pressed cell's state",
		{"frame", "button"}, [this](const Args &a, std::string &error) -> Value {
			int64_t f = 0, b = 0;
			if (!arg(a, 0, f, error) || !arg(a, 1, b, error)) return {};
			/* the press counted the stroke's rerecord; the cells after it do not */
			inputs_.countRerecords(!stroke_);
			paint(f, f, b, dragHeld_, stroke_, error);
			inputs_.countRerecords(true);
			return {};
		});
	command("paint", "holds (true) or releases (false) one button on frames first..last", {"first", "last", "button", "held"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0, b = 0;
			bool held = false;
			if (!arg(a, 0, first, error) || !arg(a, 1, last, error) || !arg(a, 2, b, error) || !arg(a, 3, held, error)) return {};
			paint(first, last, b, held, false, error);
			return {};
		});
	command("fill", "makes the inputs at least this many frames long, the new frames empty", {"frames"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t n = 0;
			if (!arg(a, 0, n, error)) return {};
			if (n < 0 || n > (int64_t{1} << 31))
			{
				error = "between 0 and 2147483648 frames";
				return {};
			}
			const int64_t before = inputs_.size();
			if (n > before)
			{
				Step step{{}, before, n};
				inputs_.resize(n);
				keep(std::move(step), false);
				edited(before, n - 1, before);
			}
			return inputs_.size();
		});
	expose("selectFirst", "the first frame picked, -1 for none", selectFirst, true);
	expose("selectLast", "the last frame picked, -1 for none", selectLast, true);
	expose("clipboardFrames", "frames copied, ready to paste", clipboardFrames);
	expose("bindMarkers", "markers move with the frames when frames are put in or taken out", bindMarkers, true);
	command("select", "picks the frames first to last (-1, -1 for none)", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!arg(a, 0, first, error) || !arg(a, 1, last, error)) return {};
			if (first > last) std::swap(first, last);
			selectFirst.set(std::max<int64_t>(-1, first));
			selectLast.set(std::max<int64_t>(-1, last));
			return {};
		});
	command("insertFrames", "puts empty frames in at a frame; the frames from it move later", {"frame", "count"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0, count = 1;
			if (!arg(a, 0, frame, error)) return {};
			if (a.size() > 1 && !arg(a, 1, count, error)) return {};
			if (frame < 0 || frame > inputs_.size() || count < 1 || count > (int64_t{1} << 24))
			{
				error = "frames go in at 0 to " + std::to_string(inputs_.size()) + ", 1 to 16777216 of them";
				return {};
			}
			std::vector<Row> rows(static_cast<size_t>(count));
			std::vector<int32_t> neutral(static_cast<size_t>(inputs_.axisCount()));
			if (!neutral.empty()) inputs_.neutralAxes(neutral.data());
			for (Row &r : rows) r.axes = neutral;
			putIn(frame, rows);
			return {};
		});
	command("deleteFrames", "takes the frames first to last out; the frames after move earlier", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!range(a, first, last, error)) return {};
			takeOut(first, last);
			return {};
		});
	command("cloneFrames", "puts a copy of the frames first to last in after them", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!range(a, first, last, error)) return {};
			std::vector<Row> rows;
			for (int64_t f = first; f <= last; f++) rows.push_back(readRow(f));
			putIn(last + 1, rows);
			return {};
		});
	command("clearFrames", "empties the frames first to last (nothing held, analog controls at rest)", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!range(a, first, last, error)) return {};
			std::vector<int32_t> neutral(static_cast<size_t>(inputs_.axisCount()));
			if (!neutral.empty()) inputs_.neutralAxes(neutral.data());
			overwrite(first, std::vector<Row>(static_cast<size_t>(last - first + 1), Row{0, neutral}));
			return {};
		});
	command("copyFrames", "keeps the frames first to last, to paste", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!range(a, first, last, error)) return {};
			clipboard_.clear();
			for (int64_t f = first; f <= last; f++) clipboard_.push_back(readRow(f));
			clipboardFrames.set(static_cast<int64_t>(clipboard_.size()));
			return clipboardFrames.get();
		});
	command("cutFrames", "keeps the frames first to last to paste, and takes them out", {"first", "last"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0;
			if (!range(a, first, last, error)) return {};
			clipboard_.clear();
			for (int64_t f = first; f <= last; f++) clipboard_.push_back(readRow(f));
			clipboardFrames.set(static_cast<int64_t>(clipboard_.size()));
			takeOut(first, last);
			return clipboardFrames.get();
		});
	command("pasteFrames", "writes the frames copied over the frames from this one", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0;
			if (!arg(a, 0, frame, error)) return {};
			if (clipboard_.empty() || frame < 0 || frame > inputs_.size())
			{
				error = clipboard_.empty() ? "nothing was copied" : "no frame " + std::to_string(frame);
				return {};
			}
			overwrite(frame, clipboard_);
			return {};
		});
	command("pasteInsertFrames", "puts the frames copied in at this frame; the frames from it move later", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0;
			if (!arg(a, 0, frame, error)) return {};
			if (clipboard_.empty() || frame < 0 || frame > inputs_.size())
			{
				error = clipboard_.empty() ? "nothing was copied" : "no frame " + std::to_string(frame);
				return {};
			}
			putIn(frame, clipboard_);
			return {};
		});
	command("truncate", "takes out every frame from this one to the end", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0;
			if (!arg(a, 0, frame, error)) return {};
			if (frame < 0 || frame >= inputs_.size())
			{
				error = "no frame " + std::to_string(frame);
				return {};
			}
			takeOut(frame, inputs_.size() - 1);
			return {};
		});
	command("undo", "takes the last edit back", {}, [this](const Args &, std::string &) -> Value {
		undoOrRedo(true);
		return {};
	}, &canUndo);
	command("redo", "does an edit taken back again", {}, [this](const Args &, std::string &) -> Value {
		undoOrRedo(false);
		return {};
	}, &canRedo);

	forgetWatches_.push_back(frames_.changed.connect([this](int64_t, int64_t) { frames_.forget(); }));
	forgetWatches_.push_back(frames_.appended.connect([this](int64_t, int64_t) { frames_.forget(); }));
	forgetWatches_.push_back(frames_.reset.connect([this] { frames_.forget(); }));
	markersWatch_ = session_.markersLoaded.connect([this](const std::vector<Marker> &loaded) {
		markers_.clear();
		for (const Marker &m : loaded) markers_[m.frame] = m;
		markerCount.set(static_cast<int64_t>(markers_.size()));
		markerRows_.reset.emit();
		frames_.reset.emit();
	});
	frameWatch_ = session_.frame.changed.connect([this] {
		currentFrame.set(session_.frame.get());
		follow();
	});
	topWatch_ = topFrame.changed.connect([this] { clampTop(); });
	greenWatch_ = session_.greenEnd.changed.connect([this] {
		const int64_t was = greenEnd.get(), now = session_.greenEnd.get();
		greenEnd.set(now);
		const int64_t first = std::min(was, now), last = std::min(std::max(was, now), inputs_.size()) - 1;
		if (last >= first) frames_.changed.emit(first, last);
	});
	buttonsWatch_ = session_.buttonsChanged.connect([this] { frames_.reset.emit(); });
	resetWatch_ = session_.inputsReset.connect([this] {
		frameCount.set(inputs_.size());
		clampTop();
		frames_.reset.emit();
	});
	changedWatch_ = session_.inputsChanged.connect([this](int64_t a, int64_t b) { frames_.changed.emit(a, b); });
	appendedWatch_ = session_.inputsAppended.connect([this](int64_t a, int64_t b) {
		frameCount.set(inputs_.size());
		frames_.appended.emit(a, b);
	});
}

void PianoRollViewModel::follow()
{
	const int64_t now = currentFrame.get(), top = topFrame.get(), shown = visibleFrames.get();
	if (!followPlayback.get() || shown <= 0) return;
	if (now < top || now >= top + shown) topFrame.set(std::max<int64_t>(0, now - shown / 2));
}

void PianoRollViewModel::clampTop()
{
	const int64_t most = std::max<int64_t>(0, inputs_.size() - 1);
	if (topFrame.get() > most) topFrame.set(most);
	else if (topFrame.get() < 0) topFrame.set(0);
}

int32_t PianoRollViewModel::buttonCount() const
{
	return static_cast<int32_t>(session_.buttonNames().size());
}

bool PianoRollViewModel::paint(int64_t first, int64_t last, int64_t b, std::optional<bool> held, bool join, std::string &error)
{
	if (first > last) std::swap(first, last);
	if (first < 0 || b < 0 || b >= std::min<int64_t>(buttonCount(), 64))
	{
		error = "no such frame or button";
		return false;
	}
	const int64_t before = inputs_.size();
	const uint64_t bit = uint64_t{1} << b;
	Step step{{}, before, 0};
	for (int64_t f = first; f <= last; f++)
	{
		const uint64_t was = inputs_.at(f);
		const uint64_t now = held ? (*held ? was | bit : was & ~bit) : was ^ bit;
		inputs_.set(f, now);
		step.frames[f] = {was, now};
	}
	step.sizeAfter = inputs_.size();
	keep(std::move(step), join);
	edited(first, last, before);
	return true;
}

void PianoRollViewModel::keep(Step step, bool join)
{
	if (join && !undo_.empty())
	{
		/* the stroke goes on: a frame it passes again keeps its first "before" */
		Step &last = undo_.back();
		for (const auto &[f, change] : step.frames)
		{
			auto it = last.frames.find(f);
			if (it == last.frames.end()) last.frames.emplace(f, change);
			else it->second.second = change.second;
		}
		last.sizeAfter = step.sizeAfter;
	}
	else
	{
		stroke_ = false;
		undo_.push_back(std::move(step));
		if (undo_.size() > kMostUndo) undo_.erase(undo_.begin());
	}
	redo_.clear();
	canUndo.set(!undo_.empty());
	canRedo.set(false);
}

void PianoRollViewModel::undoOrRedo(bool back)
{
	std::vector<Step> &from = back ? undo_ : redo_, &to = back ? redo_ : undo_;
	if (from.empty()) return;
	Step step = std::move(from.back());
	from.pop_back();
	stroke_ = false;
	const int64_t before = inputs_.size();
	const bool shifted = step.shiftCount != 0;
	/* the rows the step set, as they were or became (where frames moved,
	 * in the places after the move) */
	auto setRows = [&](bool asBefore) {
		std::map<int64_t, bool> touched;
		for (const auto &[f, change] : step.frames) touched[f] = true;
		for (const auto &[f, change] : step.axes) touched[f] = true;
		for (const auto &[f, unused] : touched)
		{
			const auto b = step.frames.find(f);
			const auto x = step.axes.find(f);
			const uint64_t mask = b != step.frames.end() ? (asBefore ? b->second.first : b->second.second) : inputs_.at(f);
			const std::vector<int32_t> *axes = x != step.axes.end() ? (asBefore ? &x->second.first : &x->second.second) : nullptr;
			inputs_.setRow(f, mask, axes != nullptr && !axes->empty() ? axes->data() : nullptr);
		}
	};
	if (back)
	{
		setRows(true);
		if (step.shiftCount > 0) inputs_.remove(step.shiftAt, step.shiftCount);
		else if (step.shiftCount < 0)
		{
			inputs_.insert(step.shiftAt, -step.shiftCount);
			for (size_t i = 0; i < step.removedButtons.size(); i++)
				inputs_.setRow(step.shiftAt + static_cast<int64_t>(i), step.removedButtons[i],
					step.removedAxes[i].empty() ? nullptr : step.removedAxes[i].data());
		}
		if (!shifted) inputs_.resize(step.sizeBefore);
		if (step.markersBefore) markers_ = *step.markersBefore;
	}
	else
	{
		if (step.shiftCount > 0) inputs_.insert(step.shiftAt, step.shiftCount);
		else if (step.shiftCount < 0) inputs_.remove(step.shiftAt, -step.shiftCount);
		if (!shifted) inputs_.resize(std::max(step.sizeAfter, before));
		setRows(false);
		if (!shifted) inputs_.resize(step.sizeAfter);
		if (step.markersAfter) markers_ = *step.markersAfter;
	}
	int64_t first = step.frames.empty() ? std::min(step.sizeBefore, step.sizeAfter) : step.frames.begin()->first;
	int64_t last = step.frames.empty() ? std::max(step.sizeBefore, step.sizeAfter) - 1 : step.frames.rbegin()->first;
	if (!step.axes.empty())
	{
		first = std::min(first, step.axes.begin()->first);
		last = std::max(last, step.axes.rbegin()->first);
	}
	const bool markersMoved = step.markersBefore.has_value();
	const int64_t shiftAt = step.shiftAt;
	to.push_back(std::move(step));
	canUndo.set(!undo_.empty());
	canRedo.set(!redo_.empty());
	if (markersMoved) markersEdited(0);
	if (shifted || inputs_.size() < before)
	{
		/* frames moved or went: the list is told so as a whole */
		const int64_t from = shifted ? shiftAt : first;
		frameCount.set(inputs_.size());
		clampTop();
		frames_.forget();
		frames_.reset.emit();
		if (from < std::max(before, inputs_.size())) session_.edited(from);
		return;
	}
	edited(first, last, before);
}

PianoRollViewModel::Row PianoRollViewModel::readRow(int64_t frame) const
{
	Row r;
	r.buttons = inputs_.at(frame);
	r.axes.resize(inputs_.axisCount());
	if (!r.axes.empty()) inputs_.axesAt(frame, r.axes.data());
	return r;
}

void PianoRollViewModel::writeRow(int64_t frame, const Row &row)
{
	inputs_.setRow(frame, row.buttons, row.axes.empty() ? nullptr : row.axes.data());
}

void PianoRollViewModel::moveMarkers(Step &step, int64_t at, int64_t by)
{
	if (!bindMarkers.get() || markers_.empty()) return;
	step.markersBefore = markers_;
	std::map<int64_t, Marker> moved;
	for (const auto &[f, m] : markers_)
	{
		/* a marker on a frame taken out goes with it */
		if (by < 0 && f >= at && f < at - by) continue;
		Marker n = m;
		if (f >= at) n.frame = f + by;
		moved[n.frame] = n;
	}
	markers_ = moved;
	step.markersAfter = markers_;
}

void PianoRollViewModel::putIn(int64_t at, const std::vector<Row> &rows)
{
	if (rows.empty()) return;
	const int64_t before = inputs_.size(), n = static_cast<int64_t>(rows.size());
	Step step;
	step.sizeBefore = before;
	step.shiftAt = at;
	step.shiftCount = n;
	std::vector<int32_t> neutral(static_cast<size_t>(inputs_.axisCount()));
	if (!neutral.empty()) inputs_.neutralAxes(neutral.data());
	moveMarkers(step, at, n);
	inputs_.insert(at, n);
	for (int64_t i = 0; i < n; i++)
	{
		writeRow(at + i, rows[static_cast<size_t>(i)]);
		step.frames[at + i] = {0, rows[static_cast<size_t>(i)].buttons};
		if (!neutral.empty()) step.axes[at + i] = {neutral, rows[static_cast<size_t>(i)].axes};
	}
	step.sizeAfter = inputs_.size();
	keep(std::move(step), false);
	if (bindMarkers.get()) markersEdited(0);
	frameCount.set(inputs_.size());
	frames_.forget();
	frames_.reset.emit();
	if (at < before) session_.edited(at);
}

void PianoRollViewModel::takeOut(int64_t first, int64_t last)
{
	const int64_t before = inputs_.size(), n = last - first + 1;
	Step step;
	step.sizeBefore = before;
	step.shiftAt = first;
	step.shiftCount = -n;
	for (int64_t f = first; f <= last; f++)
	{
		Row r = readRow(f);
		step.removedButtons.push_back(r.buttons);
		step.removedAxes.push_back(std::move(r.axes));
	}
	moveMarkers(step, first, -n);
	inputs_.remove(first, n);
	step.sizeAfter = inputs_.size();
	keep(std::move(step), false);
	if (bindMarkers.get()) markersEdited(0);
	frameCount.set(inputs_.size());
	clampTop();
	frames_.forget();
	frames_.reset.emit();
	session_.edited(first);
}

void PianoRollViewModel::overwrite(int64_t at, const std::vector<Row> &rows)
{
	if (rows.empty()) return;
	const int64_t before = inputs_.size();
	Step step;
	step.sizeBefore = before;
	for (size_t i = 0; i < rows.size(); i++)
	{
		const int64_t f = at + static_cast<int64_t>(i);
		const Row was = readRow(f);
		writeRow(f, rows[i]);
		step.frames[f] = {was.buttons, rows[i].buttons};
		if (!was.axes.empty()) step.axes[f] = {was.axes, rows[i].axes};
	}
	step.sizeAfter = inputs_.size();
	keep(std::move(step), false);
	frames_.forget();
	edited(at, at + static_cast<int64_t>(rows.size()) - 1, before);
}

bool PianoRollViewModel::range(const Args &a, int64_t &first, int64_t &last, std::string &error) const
{
	if (!arg(a, 0, first, error) || !arg(a, 1, last, error)) return false;
	if (first > last) std::swap(first, last);
	if (first < 0 || last >= inputs_.size())
	{
		error = "the frames go from 0 to " + std::to_string(inputs_.size() - 1);
		return false;
	}
	return true;
}

void PianoRollViewModel::edited(int64_t first, int64_t last, int64_t oldCount)
{
	const int64_t now = inputs_.size();
	frameCount.set(now);
	if (first < oldCount)
	{
		frames_.changed.emit(first, std::min(last, oldCount - 1));
		/* frames the machine may have run: its states after the edit no longer hold */
		session_.edited(first);
	}
	if (now > oldCount) frames_.appended.emit(oldCount, now - 1);
}

std::string PianoRollViewModel::Frames::columnName(int32_t column) const
{
	const auto &names = vm_.session_.buttonNames();
	return column >= 0 && column < static_cast<int32_t>(names.size()) ? names[static_cast<size_t>(column)] : std::string();
}

std::string PianoRollViewModel::Frames::columnShortName(int32_t column) const
{
	const std::string &mn = vm_.session_.mnemonics();
	if (column >= 0 && column < static_cast<int32_t>(mn.size()) && mn[static_cast<size_t>(column)] != '.')
		return std::string(1, mn[static_cast<size_t>(column)]);
	return columnName(column);
}

uint32_t PianoRollViewModel::Frames::rowFlags(int64_t row) const
{
	return (vm_.inputs_.lagAt(row) ? kLag : 0) | (row < vm_.greenEnd.get() ? kGreen : 0) |
		(vm_.markers_.count(row) ? kMarker : 0);
}

std::string PianoRollViewModel::Frames::rowNote(int64_t row) const
{
	const auto it = vm_.markers_.find(row);
	return it == vm_.markers_.end() ? std::string() : it->second.text;
}

void PianoRollViewModel::replaceAll(const std::vector<Marker> &markers)
{
	markers_.clear();
	for (const Marker &m : markers) markers_[m.frame] = m;
	undo_.clear();
	redo_.clear();
	canUndo.set(false);
	canRedo.set(false);
	frameCount.set(inputs_.size());
	clampTop();
	markersEdited(0);
	frames_.reset.emit();
}

void PianoRollViewModel::markersEdited(int64_t frame)
{
	markerCount.set(static_cast<int64_t>(markers_.size()));
	std::vector<Marker> all;
	for (const auto &[f, m] : markers_) all.push_back(m);
	session_.setMarkers(std::move(all));
	markerRows_.reset.emit();
	if (frame < inputs_.size()) frames_.changed.emit(frame, frame);
}

std::string PianoRollViewModel::MarkerRows::columnName(int32_t column) const
{
	static const char *const names[] = {"frame", "text", "keepState"};
	return column >= 0 && column < 3 ? names[column] : "";
}

Value PianoRollViewModel::MarkerRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	auto it = vm_.markers_.begin();
	std::advance(it, row);
	switch (column)
	{
	case 0: return it->first;
	case 1: return it->second.text;
	case 2: return it->second.keepState;
	default: return {};
	}
}

Value PianoRollViewModel::Frames::cell(int64_t row, int32_t column) const
{
	if (column < 0 || column >= 64) return false;
	if (row != cachedRow_)
	{
		cachedBits_ = vm_.inputs_.at(row);
		cachedRow_ = row;
	}
	return (cachedBits_ >> column & 1) != 0;
}

} // namespace chimera::vm
