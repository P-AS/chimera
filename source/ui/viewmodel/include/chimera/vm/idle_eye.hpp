#pragma once
/* What the picture shows when nothing is open, as Chimera's: the Chimera
 * mark, very faint and colourless, glancing about and blinking now and then,
 * its eye following the pointer across the window. It costs nothing while
 * still: a new picture is made only when what would be drawn has changed.
 * The mark's pixels are handed in (the view model reads no image files). */

#include <cstdint>
#include <random>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class IdleEyeViewModel : public ViewModel
{
public:
	explicit IdleEyeViewModel(Dispatcher &dispatcher);

	static constexpr int32_t kWidth = 480, kHeight = 360;

	/* the mark, size by size pixels, 0xAARRGGBB */
	void setMark(int32_t size, const std::vector<uint32_t> &argb);
	/* a sixtieth of a second on; gaze is where the pointer is, -1 hard left
	 * to +1 hard right. True when a new picture was made */
	bool tick(double gaze);

	Picture eye;
	Signal<> eyeChanged;
	/* pictures made so far (a still eye makes none) */
	Property<int64_t> drawn{0};
	/* 1 wide awake, 0 shut */
	float lid() const { return lid_; }

private:
	enum class Act { Waiting, Closing, Opening };
	void animate();
	void begin(Act act, int steps);
	void wait();
	bool done() { return ++step_ >= stepsTotal_; }
	float ease() const;
	int breath() const;
	void draw();
	float sample(const std::vector<float> &mask, float x, float y) const;
	int next(int from, int to) { return std::uniform_int_distribution<int>(from, to - 1)(rng_); }

	std::vector<uint32_t> buffer_ = std::vector<uint32_t>(static_cast<size_t>(kWidth) * kHeight, 0);
	std::vector<float> still_, eye_;
	int32_t srcSize_ = 0;
	std::mt19937 rng_{0x0E7E};
	float lid_ = 1.0f, gaze_ = 0.0f, gazeTarget_ = 0.0f;
	int wait_ = 120, step_ = 0, stepsTotal_ = 0;
	Act doing_ = Act::Waiting;
	int64_t frame_ = 0;
	int drawnKey_ = -1;
};

} // namespace chimera::vm
