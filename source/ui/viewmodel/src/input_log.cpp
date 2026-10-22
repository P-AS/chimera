#include "chimera/vm/input_log.hpp"

#include <mutex>

#include "chimera/engine.h"

namespace chimera::vm {

/* The shared lock guards only which machine this is: the movie's own lock is
 * the engine's. */

int64_t InputLog::size() const
{
	std::shared_lock lock(m_);
	return s_ != nullptr ? ce_session_movie_length(s_) : 0;
}

uint64_t InputLog::at(int64_t frame) const
{
	std::shared_lock lock(m_);
	uint64_t mask = 0;
	if (s_ != nullptr) ce_session_movie_get(s_, frame, &mask, nullptr);
	return mask;
}

void InputLog::read(int64_t first, std::vector<uint64_t> &out) const
{
	std::shared_lock lock(m_);
	for (size_t i = 0; i < out.size(); i++)
	{
		out[i] = 0;
		if (s_ != nullptr) ce_session_movie_get(s_, first + static_cast<int64_t>(i), &out[i], nullptr);
	}
}

bool InputLog::set(int64_t frame, uint64_t mask)
{
	std::shared_lock lock(m_);
	if (s_ == nullptr) return false;
	std::vector<int32_t> axes(static_cast<size_t>(ce_session_axis_count(s_)));
	uint64_t was = 0;
	ce_session_movie_get(s_, frame, &was, axes.empty() ? nullptr : axes.data());
	return ce_session_movie_set(s_, frame, mask, axes.empty() ? nullptr : axes.data()) == 0;
}

void InputLog::resize(int64_t frames)
{
	std::shared_lock lock(m_);
	if (s_ != nullptr) ce_session_movie_resize(s_, frames);
}

void InputLog::countRerecords(bool on)
{
	std::shared_lock lock(m_);
	if (s_ != nullptr) ce_session_rerecord_counting(s_, on ? 1 : 0);
}

size_t InputLog::axisCount() const
{
	std::shared_lock lock(m_);
	return s_ != nullptr ? static_cast<size_t>(ce_session_axis_count(s_)) : 0;
}

void InputLog::axesAt(int64_t frame, int32_t *out) const
{
	std::shared_lock lock(m_);
	if (s_ != nullptr) ce_session_movie_get(s_, frame, nullptr, out);
}

bool InputLog::setAxesAt(int64_t frame, const int32_t *values)
{
	std::shared_lock lock(m_);
	if (s_ == nullptr) return false;
	uint64_t mask = 0;
	ce_session_movie_get(s_, frame, &mask, nullptr);
	return ce_session_movie_set(s_, frame, mask, values) == 0;
}

std::string InputLog::text() const
{
	std::shared_lock lock(m_);
	if (s_ == nullptr) return {};
	uint64_t len = 0;
	const char *t = ce_session_movie_text(s_, &len);
	return std::string(t != nullptr ? t : "", static_cast<size_t>(len));
}

bool InputLog::setLag(int64_t frame, bool lag)
{
	if (frame < 0) return false;
	std::unique_lock lock(m_);
	if (frame >= static_cast<int64_t>(lags_.size())) lags_.resize(static_cast<size_t>(frame) + 1, 0);
	const uint8_t now = lag ? 1 : 0;
	if (lags_[static_cast<size_t>(frame)] == now) return false;
	lags_[static_cast<size_t>(frame)] = now;
	return true;
}

bool InputLog::lagAt(int64_t frame) const
{
	std::shared_lock lock(m_);
	return frame >= 0 && frame < static_cast<int64_t>(lags_.size()) && lags_[static_cast<size_t>(frame)] != 0;
}

void InputLog::clearLags()
{
	std::unique_lock lock(m_);
	lags_.clear();
}

void InputLog::bind(ce_session *s)
{
	std::unique_lock lock(m_);
	s_ = s;
}

} // namespace chimera::vm
