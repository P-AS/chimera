/* test_resampler.cpp - a core's sound at 44100 Hz from the rate it mixes at
 * (issue 372): as many samples as a second at 44100 holds, a tone at the pitch
 * it had, its level kept, and a full-scale sound held at the edge rather than
 * wrapped round to the other one.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/resampler.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;

/* `seconds` of a tone at `hz` and `level` (of full scale), at `rate`, in frames of `perFrame` */
std::vector<int16_t> run(int rate, double hz, double level, int seconds, int perFrame)
{
	chimera::Resampler r;
	r.configure(rate, 44100);
	std::vector<int16_t> out, frame;
	int64_t n = 0;
	const int64_t total = static_cast<int64_t>(rate) * seconds;
	for (int64_t done = 0; done < total; done += perFrame)
	{
		frame.clear();
		const int count = static_cast<int>(std::min<int64_t>(perFrame, total - done));
		for (int i = 0; i < count; i++, n++)
		{
			const auto v = static_cast<int16_t>(std::lround(32767.0 * level * std::sin(2 * kPi * hz * n / rate)));
			frame.push_back(v);
			frame.push_back(v);
		}
		r.process(frame.data(), static_cast<size_t>(count), out);
	}
	return out;
}

/* the tone's pitch, from its rising zero crossings over the second half */
double pitch(const std::vector<int16_t> &s)
{
	const size_t pairs = s.size() / 2, from = pairs / 2;
	int crossings = 0;
	for (size_t i = from + 1; i < pairs; i++)
		if (s[2 * (i - 1)] < 0 && s[2 * i] >= 0) crossings++;
	return crossings * 44100.0 / static_cast<double>(pairs - from);
}

int peak(const std::vector<int16_t> &s)
{
	int most = 0;
	for (size_t i = s.size() / 2; i < s.size(); i++) most = std::max(most, std::abs(static_cast<int>(s[i])));
	return most;
}
} // namespace

int main()
{
	/* a PS2's or MAME's 48 kHz, in frames of 800: two seconds make two
	 * seconds at 44100, short only by the filter's few samples of delay */
	{
		const auto out = run(48000, 1000.0, 0.5, 2, 800);
		std::printf("48000 Hz: %zu samples for 2 s, pitch %.1f Hz, peak %d\n", out.size() / 2, pitch(out), peak(out));
		assert(std::llabs(static_cast<long long>(out.size() / 2) - 88200) <= 32);
		assert(std::fabs(pitch(out) - 1000.0) < 2.0);
		assert(std::abs(peak(out) - 16384) < 400);
	}
	/* a Game Boy's 32768 Hz and a Uzebox's 15734 Hz, upwards */
	{
		const auto gb = run(32768, 440.0, 0.5, 2, 546);
		assert(std::llabs(static_cast<long long>(gb.size() / 2) - 88200) <= 64);
		assert(std::fabs(pitch(gb) - 440.0) < 2.0);
		const auto uz = run(15734, 300.0, 0.5, 2, 262);
		assert(std::llabs(static_cast<long long>(uz.size() / 2) - 88200) <= 128);
		assert(std::fabs(pitch(uz) - 300.0) < 2.0);
	}
	/* a full-scale square wave rings past full scale through the filter:
	 * held at the edge, never wrapped round (a wrap is a loud click) */
	{
		chimera::Resampler r;
		r.configure(48000, 44100);
		std::vector<int16_t> in, out;
		for (int i = 0; i < 48000; i++)
		{
			const int16_t v = (i / 24) % 2 ? 32767 : -32768;
			in.push_back(v);
			in.push_back(v);
		}
		r.process(in.data(), 48000, out);
		bool wrapped = false;
		for (size_t i = 2; i < out.size(); i += 2)
			wrapped = wrapped || std::abs(static_cast<int>(out[i]) - static_cast<int>(out[i - 2])) > 60000;
		assert(!wrapped);
	}
	/* 44100 to 44100: what went in comes out, a few samples later */
	{
		const auto same = run(44100, 1000.0, 0.5, 1, 735);
		assert(std::llabs(static_cast<long long>(same.size() / 2) - 44100) <= 32);
		assert(std::fabs(pitch(same) - 1000.0) < 2.0);
	}
	std::printf("resampler: ok\n");
	return 0;
}
