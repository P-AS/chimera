#pragma once
/* The three things a view model is made of: signals (something happened),
 * properties (a value, and a signal when it changes) and the connections that
 * end a subscription. All of it lives on the view's thread: a view model that
 * learns something on another thread posts it there first (Dispatcher). */

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace chimera::vm {

/* Ends a subscription when it is destroyed or disconnected; the signal may
 * already be gone by then. */
class Connection
{
public:
	Connection() = default;
	explicit Connection(std::function<void()> undo) : undo_(std::move(undo)) {}
	Connection(Connection &&o) noexcept : undo_(std::exchange(o.undo_, nullptr)) {}
	Connection &operator=(Connection &&o) noexcept
	{
		if (this != &o)
		{
			disconnect();
			undo_ = std::exchange(o.undo_, nullptr);
		}
		return *this;
	}
	Connection(const Connection &) = delete;
	Connection &operator=(const Connection &) = delete;
	~Connection() { disconnect(); }

	void disconnect()
	{
		if (undo_)
		{
			auto undo = std::exchange(undo_, nullptr);
			undo();
		}
	}

private:
	std::function<void()> undo_;
};

template <class... A>
class Signal
{
public:
	[[nodiscard]] Connection connect(std::function<void(A...)> fn)
	{
		const uint64_t id = slots_->next++;
		slots_->list.emplace_back(id, std::move(fn));
		std::weak_ptr<Slots> weak = slots_;
		return Connection([weak, id] {
			if (auto s = weak.lock())
				std::erase_if(s->list, [id](const auto &e) { return e.first == id; });
		});
	}

	void emit(A... args) const
	{
		if (slots_->list.empty()) return;
		/* a slot may connect or disconnect others while it runs: call each
		 * one that was there at the start and is still there now */
		const auto snapshot = slots_->list;
		for (const auto &[id, fn] : snapshot)
		{
			const bool still = std::any_of(slots_->list.begin(), slots_->list.end(),
				[id = id](const auto &e) { return e.first == id; });
			if (still) fn(args...);
		}
	}

private:
	struct Slots
	{
		std::vector<std::pair<uint64_t, std::function<void(A...)>>> list;
		uint64_t next = 1;
	};
	std::shared_ptr<Slots> slots_ = std::make_shared<Slots>();
};

template <class T>
class Property
{
public:
	Property() = default;
	explicit Property(T initial) : value_(std::move(initial)) {}

	const T &get() const { return value_; }

	/* true when the value changed, and `changed` was emitted */
	bool set(T v)
	{
		if (v == value_) return false;
		value_ = std::move(v);
		changed.emit();
		return true;
	}

	Signal<> changed;

private:
	T value_{};
};

/* Where a view model sends work that must run on the view's thread. The
 * window gives one that queues into its event loop; a test gives one it
 * drains by hand. */
class Dispatcher
{
public:
	virtual ~Dispatcher() = default;
	virtual void post(std::function<void()> fn) = 0;
};

} // namespace chimera::vm
