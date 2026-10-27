#pragma once
/* A core's sound at the rate Chimera plays and encodes at (44100 Hz), from
 * the rate the core mixes at (its package's audio.rate, or its machine's
 * audioRate): a windowed-sinc filter, fed a frame's samples at a time and
 * keeping what it needs of the frames before (issue 372; the window's own
 * resampler had been lost with the old window). Stereo, interleaved. */

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chimera {

class Resampler
{
public:
	/* from inRate to outRate, history emptied */
	void configure(int32_t inRate, int32_t outRate);
	/* the sound before is forgotten (a seek, a state loaded) */
	void reset();
	/* `pairs` stereo samples in; what they make at the other rate appended
	 * to `out` (a few samples of each frame wait for the next) */
	void process(const int16_t *in, size_t pairs, std::vector<int16_t> &out);

private:
	/* taps each side of the point sampled, and the table's fractions of a sample */
	static constexpr int kHalf = 16;
	static constexpr int kPhases = 256;
	double step_ = 1.0;
	/* where the next output falls, counted in input samples from history_'s start */
	double position_ = 0.0;
	std::vector<float> history_;
	/* kPhases + 1 rows of 2 * kHalf weights each */
	std::vector<float> table_;
};

} // namespace chimera
