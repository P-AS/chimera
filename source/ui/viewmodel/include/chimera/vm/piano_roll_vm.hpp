#pragma once
/* The piano roll: every frame a row, every button a column, a cell lit where
 * the button is held. It reads and edits the same inputs the session plays. */

#include "chimera/vm/input_log.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class PianoRollViewModel : public ViewModel
{
public:
	/* Frames::rowFlags */
	static constexpr uint32_t kLag = 1, kGreen = 2;

	PianoRollViewModel(Dispatcher &dispatcher, InputLog &inputs, SessionViewModel &session);

	/* the frame the machine is about to run */
	Property<int64_t> currentFrame{0};
	Property<int64_t> frameCount{0};
	/* keep the current frame in view while the machine runs */
	Property<bool> followPlayback{true};
	/* frames before this one were run with the inputs they have now: the greenzone */
	Property<int64_t> greenEnd{0};
	/* the first frame in view, and how many fit: the view says how many */
	Property<int64_t> topFrame{0};
	Property<int64_t> visibleFrames{0};

	/* the rows, for a view that draws its own cells (cells as bits, no Value) */
	uint64_t frameBits(int64_t frame) const { return inputs_.at(frame); }
	void readBits(int64_t first, std::vector<uint64_t> &out) const { inputs_.read(first, out); }
	int32_t buttonCount() const;
	RowModel &frames() { return frames_; }

private:
	class Frames : public RowModel
	{
	public:
		explicit Frames(PianoRollViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return vm_.inputs_.size(); }
		int32_t columnCount() const override { return vm_.buttonCount(); }
		std::string columnName(int32_t column) const override;
		/* the button's letter in a movie's input log */
		std::string columnShortName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;
		uint32_t rowFlags(int64_t row) const override;

	private:
		PianoRollViewModel &vm_;
	};

	void edited(int64_t first, int64_t last, int64_t oldCount);
	void follow();
	void clampTop();

	InputLog &inputs_;
	SessionViewModel &session_;
	Frames frames_{*this};
	Connection frameWatch_, buttonsWatch_, changedWatch_, appendedWatch_, topWatch_, greenWatch_, resetWatch_;
	/* what a drag paints: the state the pressed cell was given */
	bool dragHeld_ = false;
};

} // namespace chimera::vm
