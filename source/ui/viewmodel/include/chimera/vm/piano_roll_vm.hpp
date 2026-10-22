#pragma once
/* The piano roll: every frame a row, every button a column, a cell lit where
 * the button is held. It reads and edits the same inputs the session plays. */

#include <map>
#include <optional>

#include "chimera/vm/input_log.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class PianoRollViewModel : public ViewModel
{
public:
	/* Frames::rowFlags */
	static constexpr uint32_t kLag = 1, kGreen = 2, kMarker = 4;

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
	Property<int64_t> markerCount{0};
	/* an edit can be taken back, or one taken back done again */
	Property<bool> canUndo{false};
	Property<bool> canRedo{false};

	/* the rows, for a view that draws its own cells (cells as bits, no Value) */
	uint64_t frameBits(int64_t frame) const { return inputs_.at(frame); }
	void readBits(int64_t first, std::vector<uint64_t> &out) const { inputs_.read(first, out); }
	int32_t buttonCount() const;
	RowModel &frames() { return frames_; }
	const std::map<int64_t, Marker> &markers() const { return markers_; }
	/* the inputs were replaced whole (a branch loaded), with these markers:
	 * the rows start again, and what could be undone is forgotten */
	void replaceAll(const std::vector<Marker> &markers);

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
		std::string rowNote(int64_t row) const override;
		/* the row read last: a view asks for a row's cells one after another,
		 * and each read is the engine's (a line to parse). Forgotten on every
		 * change of the rows. */
		void forget() const { cachedRow_ = -1; }

	private:
		PianoRollViewModel &vm_;
		mutable int64_t cachedRow_ = -1;
		mutable uint64_t cachedBits_ = 0;
	};

	class MarkerRows : public RowModel
	{
	public:
		explicit MarkerRows(PianoRollViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.markers_.size()); }
		int32_t columnCount() const override { return 3; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		PianoRollViewModel &vm_;
	};

	/* One edit, for undo: each frame it changed, as it was and as it became,
	 * and the length before and after. */
	struct Step
	{
		std::map<int64_t, std::pair<uint64_t, uint64_t>> frames;
		int64_t sizeBefore = 0, sizeAfter = 0;
	};
	static constexpr size_t kMostUndo = 1000;

	/* held: set to it; nullopt: flip. join: part of the stroke under way */
	bool paint(int64_t first, int64_t last, int64_t button, std::optional<bool> held, bool join, std::string &error);
	void keep(Step step, bool join);
	void undoOrRedo(bool back);
	void edited(int64_t first, int64_t last, int64_t oldCount);
	void markersEdited(int64_t frame);
	void follow();
	void clampTop();

	InputLog &inputs_;
	SessionViewModel &session_;
	Frames frames_{*this};
	std::map<int64_t, Marker> markers_;
	MarkerRows markerRows_{*this};
	Connection markersWatch_;
	std::vector<Connection> forgetWatches_;
	Connection frameWatch_, buttonsWatch_, changedWatch_, appendedWatch_, topWatch_, greenWatch_, resetWatch_;
	/* what a drag paints: the state the pressed cell was given */
	bool dragHeld_ = false;
	/* a press began a stroke that drags extend */
	bool stroke_ = false;
	std::vector<Step> undo_, redo_;
};

} // namespace chimera::vm
