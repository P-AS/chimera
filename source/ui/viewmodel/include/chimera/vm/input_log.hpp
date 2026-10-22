#pragma once
/* The buttons held on every frame, one bit per button (the first 64), shared
 * by the session, which plays and records them, and the piano roll, which
 * shows and edits them. Either may touch it from its own thread.
 *
 * Beside them, what the machine said of each frame it ran: whether it was a
 * lag frame (the game did not read its input). That is the machine's, not
 * the movie's, and an edit leaves it until the frame is run again. */

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace chimera::vm {

class InputLog
{
public:
	int64_t size() const
	{
		std::shared_lock lock(m_);
		return static_cast<int64_t>(masks_.size());
	}

	/* nothing held past the end */
	uint64_t at(int64_t frame) const
	{
		std::shared_lock lock(m_);
		return frame >= 0 && frame < static_cast<int64_t>(masks_.size()) ? masks_[static_cast<size_t>(frame)] : 0;
	}

	/* copies frames first.. into out (as many as out holds), nothing past the end */
	void read(int64_t first, std::vector<uint64_t> &out) const
	{
		std::shared_lock lock(m_);
		for (size_t i = 0; i < out.size(); i++)
		{
			const int64_t f = first + static_cast<int64_t>(i);
			out[i] = f >= 0 && f < static_cast<int64_t>(masks_.size()) ? masks_[static_cast<size_t>(f)] : 0;
		}
	}

	/* grows the log with empty frames when frame is past its end */
	void set(int64_t frame, uint64_t mask)
	{
		if (frame < 0) return;
		std::unique_lock lock(m_);
		if (frame >= static_cast<int64_t>(masks_.size())) masks_.resize(static_cast<size_t>(frame) + 1, 0);
		masks_[static_cast<size_t>(frame)] = mask;
	}

	void resize(int64_t frames)
	{
		std::unique_lock lock(m_);
		masks_.resize(static_cast<size_t>(frames < 0 ? 0 : frames), 0);
	}

	/* Axis values (sticks, paddles): `count` per frame in the machine's
	 * order, each at its neutral until a frame sets it. Setting the count
	 * forgets every frame's values. */
	void setAxes(const std::vector<int32_t> &neutral)
	{
		std::unique_lock lock(m_);
		neutral_ = neutral;
		axes_.clear();
	}
	size_t axisCount() const
	{
		std::shared_lock lock(m_);
		return neutral_.size();
	}
	/* out gets axisCount() values */
	void axesAt(int64_t frame, int32_t *out) const
	{
		std::shared_lock lock(m_);
		const size_t n = neutral_.size(), at = static_cast<size_t>(frame) * n;
		for (size_t i = 0; i < n; i++)
			out[i] = frame >= 0 && at + n <= axes_.size() ? axes_[at + i] : neutral_[i];
	}
	void setAxesAt(int64_t frame, const int32_t *values)
	{
		if (frame < 0) return;
		std::unique_lock lock(m_);
		const size_t n = neutral_.size();
		if (n == 0) return;
		const size_t at = static_cast<size_t>(frame) * n;
		if (axes_.size() < at + n)
		{
			/* the frames between keep their neutral values */
			const size_t from = axes_.size() / n;
			axes_.resize(at + n);
			for (size_t f = from; f * n < at; f++)
				for (size_t i = 0; i < n; i++) axes_[f * n + i] = neutral_[i];
		}
		for (size_t i = 0; i < n; i++) axes_[at + i] = values[i];
	}

	/* every frame at once (a project's movie): masks, and axes when the
	 * machine has any (axisCount() values a frame); the lag flags go */
	void assign(std::vector<uint64_t> masks, std::vector<int32_t> axes)
	{
		std::unique_lock lock(m_);
		masks_ = std::move(masks);
		axes_ = std::move(axes);
		lags_.clear();
	}

	/* true when the frame's lag flag changed */
	bool setLag(int64_t frame, bool lag)
	{
		if (frame < 0) return false;
		std::unique_lock lock(m_);
		if (frame >= static_cast<int64_t>(lags_.size())) lags_.resize(static_cast<size_t>(frame) + 1, 0);
		const uint8_t now = lag ? 1 : 0;
		if (lags_[static_cast<size_t>(frame)] == now) return false;
		lags_[static_cast<size_t>(frame)] = now;
		return true;
	}

	bool lagAt(int64_t frame) const
	{
		std::shared_lock lock(m_);
		return frame >= 0 && frame < static_cast<int64_t>(lags_.size()) && lags_[static_cast<size_t>(frame)] != 0;
	}

private:
	mutable std::shared_mutex m_;
	std::vector<uint64_t> masks_;
	std::vector<uint8_t> lags_;
	std::vector<int32_t> neutral_, axes_;
};

} // namespace chimera::vm
