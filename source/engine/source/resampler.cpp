#include "resampler.hpp"

#include <algorithm>
#include <cmath>

namespace chimera {

namespace {
constexpr double kPi = 3.14159265358979323846;

double sinc(double x) { return std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x); }

/* Blackman's window over -1..1 */
double window(double x)
{
	if (std::fabs(x) >= 1.0) return 0.0;
	const double t = (x + 1.0) / 2.0;
	return 0.42 - 0.5 * std::cos(2.0 * kPi * t) + 0.08 * std::cos(4.0 * kPi * t);
}
} // namespace

void Resampler::configure(int32_t inRate, int32_t outRate)
{
	step_ = inRate > 0 && outRate > 0 ? static_cast<double>(inRate) / outRate : 1.0;
	/* nothing above the lower rate's half survives, a little below it so the
	 * filter's slope has room: an alias is a tone the game never made */
	const double cutoff = std::min(1.0, 1.0 / step_) * 0.92;
	table_.assign(static_cast<size_t>(kPhases + 1) * 2 * kHalf, 0.0f);
	for (int p = 0; p <= kPhases; p++)
	{
		const double fraction = static_cast<double>(p) / kPhases;
		double sum = 0.0;
		float *row = &table_[static_cast<size_t>(p) * 2 * kHalf];
		for (int j = 0; j < 2 * kHalf; j++)
		{
			const double d = j - kHalf + 1 - fraction;
			const double w = cutoff * sinc(cutoff * d) * window(d / kHalf);
			row[j] = static_cast<float>(w);
			sum += w;
		}
		/* a steady level comes out at the level it went in */
		for (int j = 0; j < 2 * kHalf; j++) row[j] = static_cast<float>(row[j] / sum);
	}
	reset();
}

void Resampler::reset()
{
	/* silence before the first sample, so the filter has a past to look at */
	history_.assign(static_cast<size_t>(kHalf - 1) * 2, 0.0f);
	position_ = kHalf - 1;
}

void Resampler::process(const int16_t *in, size_t pairs, std::vector<int16_t> &out)
{
	if (table_.empty()) configure(44100, 44100);
	for (size_t i = 0; i < pairs * 2; i++) history_.push_back(static_cast<float>(in[i]));
	const int64_t frames = static_cast<int64_t>(history_.size() / 2);
	while (true)
	{
		const int64_t at = static_cast<int64_t>(std::floor(position_));
		/* the last tap must have arrived */
		if (at + kHalf >= frames) break;
		const double fraction = position_ - static_cast<double>(at);
		const double phase = fraction * kPhases;
		const int p = std::min(kPhases - 1, static_cast<int>(phase));
		const float mix = static_cast<float>(phase - p);
		const float *a = &table_[static_cast<size_t>(p) * 2 * kHalf];
		const float *b = a + 2 * kHalf;
		const float *x = &history_[static_cast<size_t>(at - kHalf + 1) * 2];
		float left = 0.0f, right = 0.0f;
		for (int j = 0; j < 2 * kHalf; j++)
		{
			const float w = a[j] + (b[j] - a[j]) * mix;
			left += x[2 * j] * w;
			right += x[2 * j + 1] * w;
		}
		/* a loud sample is held at the edge, never wrapped round to the other one */
		out.push_back(static_cast<int16_t>(std::clamp(std::lround(left), -32768L, 32767L)));
		out.push_back(static_cast<int16_t>(std::clamp(std::lround(right), -32768L, 32767L)));
		position_ += step_;
	}
	/* what no output will look at again goes */
	const int64_t used = static_cast<int64_t>(std::floor(position_)) - (kHalf - 1);
	if (used > 0)
	{
		history_.erase(history_.begin(), history_.begin() + used * 2);
		position_ -= static_cast<double>(used);
	}
}

} // namespace chimera
