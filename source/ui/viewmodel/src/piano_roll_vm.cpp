#include "chimera/vm/piano_roll_vm.hpp"

#include <algorithm>

namespace chimera::vm {

PianoRollViewModel::PianoRollViewModel(Dispatcher &dispatcher, InputLog &inputs, SessionViewModel &session)
	: ViewModel("pianoRoll", dispatcher), inputs_(inputs), session_(session)
{
	expose("currentFrame", "the frame the machine is about to run", currentFrame);
	expose("frameCount", "how many frames have inputs", frameCount);
	expose("followPlayback", "keep the current frame in view while the machine runs", followPlayback, true);
	expose("topFrame", "the first frame in view", topFrame, true);
	expose("visibleFrames", "how many frames the view shows at once (the view sets it)", visibleFrames, true);
	rows("frames", "one row a frame, one column a button; a cell is true where the button is held", frames_);

	command("toggle", "flips one button on one frame", {"frame", "button"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0, b = 0;
			if (!arg(a, 0, f, error) || !arg(a, 1, b, error)) return {};
			if (f < 0 || b < 0 || b >= std::min<int64_t>(buttonCount(), 64))
			{
				error = "no such frame or button";
				return {};
			}
			const int64_t before = inputs_.size();
			inputs_.set(f, inputs_.at(f) ^ (uint64_t{1} << b));
			edited(f, f, before);
			return (inputs_.at(f) >> b & 1) != 0;
		});
	command("press", "a press on a cell: flips it, and a drag from it gives the cells it passes the same state",
		{"frame", "button"}, [this](const Args &a, std::string &error) -> Value {
			std::string why;
			const auto held = run("toggle", a, why);
			if (!held) { error = why; return {}; }
			fromValue(*held, dragHeld_);
			return *held;
		});
	command("drag", "a drag from a pressed cell reached this one: it takes the pressed cell's state",
		{"frame", "button"}, [this](const Args &a, std::string &error) -> Value {
			int64_t f = 0, b = 0;
			if (!arg(a, 0, f, error) || !arg(a, 1, b, error)) return {};
			std::string why;
			if (!run("paint", {f, f, b, dragHeld_}, why)) error = why;
			return {};
		});
	command("paint", "holds (true) or releases (false) one button on frames first..last", {"first", "last", "button", "held"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t first = 0, last = 0, b = 0;
			bool held = false;
			if (!arg(a, 0, first, error) || !arg(a, 1, last, error) || !arg(a, 2, b, error) || !arg(a, 3, held, error)) return {};
			if (first > last) std::swap(first, last);
			if (first < 0 || b < 0 || b >= std::min<int64_t>(buttonCount(), 64))
			{
				error = "no such frame or button";
				return {};
			}
			const int64_t before = inputs_.size();
			const uint64_t bit = uint64_t{1} << b;
			for (int64_t f = first; f <= last; f++)
				inputs_.set(f, held ? inputs_.at(f) | bit : inputs_.at(f) & ~bit);
			edited(first, last, before);
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
				inputs_.resize(n);
				edited(before, n - 1, before);
			}
			return inputs_.size();
		});

	frameWatch_ = session_.frame.changed.connect([this] {
		currentFrame.set(session_.frame.get());
		follow();
	});
	topWatch_ = topFrame.changed.connect([this] { clampTop(); });
	buttonsWatch_ = session_.buttonsChanged.connect([this] { frames_.reset.emit(); });
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

void PianoRollViewModel::edited(int64_t first, int64_t last, int64_t oldCount)
{
	const int64_t now = inputs_.size();
	frameCount.set(now);
	if (first < oldCount) frames_.changed.emit(first, std::min(last, oldCount - 1));
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

Value PianoRollViewModel::Frames::cell(int64_t row, int32_t column) const
{
	if (column < 0 || column >= 64) return false;
	return (vm_.inputs_.at(row) >> column & 1) != 0;
}

} // namespace chimera::vm
