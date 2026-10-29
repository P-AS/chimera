#pragma once
/* Which of a controller's buttons are pressed: one bit a button, in the
 * controller's order, as many as it has. A keyboard is a hundred keys and
 * Mednafen's controllers are 594 buttons; the window carried the first 64 in
 * one number and lost the rest (issue 411). */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace chimera::vm {

class Buttons
{
public:
	Buttons() = default;
	/* the first 64 buttons from a number, one bit each (a test's, a script's) */
	static Buttons first64(uint64_t bits)
	{
		Buttons b;
		if (bits != 0) b.words_.push_back(bits);
		return b;
	}
	/* from the engine's form: one byte a button, nonzero pressed */
	static Buttons fromStates(const uint8_t *states, size_t count)
	{
		Buttons b;
		for (size_t i = 0; i < count; i++)
			if (states[i] != 0) b.set(i);
		return b;
	}
	/* to the engine's form, `count` bytes */
	std::vector<uint8_t> states(size_t count) const
	{
		std::vector<uint8_t> out(count, 0);
		for (size_t i = 0; i < count; i++) out[i] = test(i) ? 1 : 0;
		return out;
	}

	bool test(size_t i) const { return i / 64 < words_.size() && ((words_[i / 64] >> (i % 64)) & 1) != 0; }
	void set(size_t i, bool on = true)
	{
		if (!on)
		{
			if (i / 64 < words_.size()) words_[i / 64] &= ~(uint64_t{1} << (i % 64));
			return;
		}
		if (i / 64 >= words_.size()) words_.resize(i / 64 + 1, 0);
		words_[i / 64] |= uint64_t{1} << (i % 64);
	}
	void flip(size_t i) { set(i, !test(i)); }
	bool any() const
	{
		return std::any_of(words_.begin(), words_.end(), [](uint64_t w) { return w != 0; });
	}
	void clear() { words_.clear(); }
	/* the first 64, as one number (a script's joypad, the tests) */
	uint64_t low() const { return words_.empty() ? 0 : words_[0]; }
	/* f(i) for each button pressed, in order */
	template <class F>
	void each(F &&f) const
	{
		for (size_t w = 0; w < words_.size(); w++)
			for (uint64_t bits = words_[w]; bits != 0; bits &= bits - 1)
				f(w * 64 + static_cast<size_t>(__builtin_ctzll(bits)));
	}

	Buttons &operator|=(const Buttons &o)
	{
		if (o.words_.size() > words_.size()) words_.resize(o.words_.size(), 0);
		for (size_t i = 0; i < o.words_.size(); i++) words_[i] |= o.words_[i];
		return *this;
	}
	Buttons &operator^=(const Buttons &o)
	{
		if (o.words_.size() > words_.size()) words_.resize(o.words_.size(), 0);
		for (size_t i = 0; i < o.words_.size(); i++) words_[i] ^= o.words_[i];
		return *this;
	}
	Buttons &operator&=(const Buttons &o)
	{
		for (size_t i = 0; i < words_.size(); i++) words_[i] &= word(o, i);
		return *this;
	}
	Buttons operator|(const Buttons &o) const
	{
		Buttons r = *this;
		r |= o;
		return r;
	}
	Buttons operator&(const Buttons &o) const
	{
		Buttons r = *this;
		r &= o;
		return r;
	}
	/* these, less the ones in o */
	Buttons without(const Buttons &o) const
	{
		Buttons r = *this;
		for (size_t i = 0; i < r.words_.size(); i++) r.words_[i] &= ~word(o, i);
		return r;
	}
	bool operator==(const Buttons &o) const
	{
		for (size_t i = 0; i < std::max(words_.size(), o.words_.size()); i++)
			if (word(*this, i) != word(o, i)) return false;
		return true;
	}
	bool operator!=(const Buttons &o) const { return !(*this == o); }

private:
	std::vector<uint64_t> words_;
	static uint64_t word(const Buttons &b, size_t i) { return i < b.words_.size() ? b.words_[i] : 0; }
};

/* Buttons one thread sets and another reads (what is held, set on the
 * window's thread and played on the machine's) */
class SharedButtons
{
public:
	Buttons get() const
	{
		std::lock_guard<std::mutex> lock(m_);
		return b_;
	}
	void set(Buttons b)
	{
		std::lock_guard<std::mutex> lock(m_);
		b_ = std::move(b);
	}
	/* what it held, and it holds b from now */
	Buttons exchange(Buttons b)
	{
		std::lock_guard<std::mutex> lock(m_);
		std::swap(b_, b);
		return b;
	}
	void add(size_t i)
	{
		std::lock_guard<std::mutex> lock(m_);
		b_.set(i);
	}
	void flip(size_t i)
	{
		std::lock_guard<std::mutex> lock(m_);
		b_.flip(i);
	}
	/* each of these pressed when it was not, and let go when it was */
	void toggle(const Buttons &these)
	{
		std::lock_guard<std::mutex> lock(m_);
		b_ ^= these;
	}
	bool test(size_t i) const
	{
		std::lock_guard<std::mutex> lock(m_);
		return b_.test(i);
	}

private:
	mutable std::mutex m_;
	Buttons b_;
};

} // namespace chimera::vm
