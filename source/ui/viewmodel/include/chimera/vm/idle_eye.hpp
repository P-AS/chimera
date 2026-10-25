#pragma once
/* What the picture shows when nothing is open, as Chimera's: the Chimera
 * mark, very faint and colourless, glancing about and blinking now and then,
 * its eye following the pointer across the window. It costs nothing while
 * still: a new picture is made only when what would be drawn has changed.
 * The mark's pixels are handed in (the view model reads no image files).
 *
 * Or snow, as Chimera's "Snowy null core" (Config > Display, Misc): white
 * noise 256 x 192, new snowFramerate times fewer than sixty a second,
 * each pixel's grey a random byte raised to 10^-bias and times the
 * brightness, as Chimera's SnowyNullVideo makes it. Never, always, or
 * (Chimera's default) the week of Christmas, its days 354 to 360 of the
 * year. */

#include <cstdint>
#include <functional>
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

	/* when there is snow: 0 never, 1 always, 2 the week of Christmas; the
	 * brightness (0.1 to 1), the bias (-2 to 2), and the rate as a divisor
	 * of sixty a second (1 to 20) */
	Property<int64_t> snowWhen{2};
	Property<double> snowIntensity{1.0}, snowBias{-2.0};
	Property<int64_t> snowFramerate{10};
	static constexpr int32_t kSnowWidth = 256, kSnowHeight = 192;
	/* the day of the year, 1 for the first of January (the local date at first) */
	std::function<int()> dayOfYear;
	bool snowing() const;

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
	/* the snow: its pixels, its own numbers (Chimera's seed), the turns since the last */
	void snow();
	std::vector<uint32_t> snow_ = std::vector<uint32_t>(static_cast<size_t>(kSnowWidth) * kSnowHeight, 0xFF000000u);
	std::mt19937 snowRng_{0x4};
	int64_t snowTurn_ = 0;
	bool wasSnowing_ = false;
};

} // namespace chimera::vm
