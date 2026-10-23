#include "chimera/vm/input_log.hpp"

#include <algorithm>
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

bool InputLog::setRow(int64_t frame, uint64_t mask, const int32_t *axes)
{
	std::shared_lock lock(m_);
	if (s_ == nullptr) return false;
	if (axes != nullptr) return ce_session_movie_set(s_, frame, mask, axes) == 0;
	std::vector<int32_t> was(static_cast<size_t>(ce_session_axis_count(s_)));
	uint64_t old = 0;
	ce_session_movie_get(s_, frame, &old, was.empty() ? nullptr : was.data());
	return ce_session_movie_set(s_, frame, mask, was.empty() ? nullptr : was.data()) == 0;
}

void InputLog::insert(int64_t frame, int64_t count)
{
	std::unique_lock lock(m_);
	if (s_ == nullptr || frame < 0 || count <= 0) return;
	ce_session_movie_insert(s_, frame, count);
	if (frame < static_cast<int64_t>(lags_.size())) lags_.insert(lags_.begin() + frame, static_cast<size_t>(count), kNotRun);
}

void InputLog::remove(int64_t frame, int64_t count)
{
	std::unique_lock lock(m_);
	if (s_ == nullptr || frame < 0 || count <= 0) return;
	ce_session_movie_remove(s_, frame, count);
	if (frame < static_cast<int64_t>(lags_.size()))
		lags_.erase(lags_.begin() + frame, lags_.begin() + std::min<int64_t>(frame + count, static_cast<int64_t>(lags_.size())));
}

void InputLog::neutralAxes(int32_t *out) const
{
	std::shared_lock lock(m_);
	if (s_ == nullptr) return;
	for (int64_t i = 0; i < ce_session_axis_count(s_); i++) out[i] = ce_session_axis_neutral(s_, i);
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

std::vector<std::string> InputLog::lines(int64_t first, int64_t last) const
{
	std::vector<std::string> out;
	const std::string all = text();
	ce_movie_log *log = ce_movie_log_new();
	if (ce_movie_log_parse(log, all.data(), all.size()) == 0)
		for (int64_t f = std::max<int64_t>(first, 0); f <= last; f++)
			if (const char *entry = ce_movie_log_entry(log, f)) out.emplace_back(entry);
	ce_movie_log_free(log);
	return out;
}

bool InputLog::ranAt(int64_t frame) const
{
	std::shared_lock lock(m_);
	return frame >= 0 && frame < static_cast<int64_t>(lags_.size()) && lags_[static_cast<size_t>(frame)] != kNotRun;
}

bool InputLog::setLag(int64_t frame, bool lag)
{
	if (frame < 0) return false;
	std::unique_lock lock(m_);
	if (frame >= static_cast<int64_t>(lags_.size())) lags_.resize(static_cast<size_t>(frame) + 1, kNotRun);
	const uint8_t now = lag ? 1 : 0;
	if (lags_[static_cast<size_t>(frame)] == now) return false;
	lags_[static_cast<size_t>(frame)] = now;
	return true;
}

bool InputLog::forgetLag(int64_t frame)
{
	std::unique_lock lock(m_);
	if (frame < 0 || frame >= static_cast<int64_t>(lags_.size()) || lags_[static_cast<size_t>(frame)] == kNotRun) return false;
	lags_[static_cast<size_t>(frame)] = kNotRun;
	return true;
}

bool InputLog::lagAt(int64_t frame) const
{
	std::shared_lock lock(m_);
	return frame >= 0 && frame < static_cast<int64_t>(lags_.size()) && lags_[static_cast<size_t>(frame)] == 1;
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
