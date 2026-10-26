#pragma once
/* The movie, as the view models see it: the engine's own (owner, 2026-10-22:
 * the engine owns the movie). Every read and edit goes to the open machine's
 * movie, which the engine keeps, writes and takes edits for from any thread;
 * with no machine open there is no movie. The engine writes every line.
 *
 * Beside it, what the machine said of each frame it ran: whether it was a lag
 * frame (the game did not read its input). That is the machine's, not the
 * movie's, and is kept here. */

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

struct ce_session;

namespace chimera::vm {

class InputLog
{
public:
	int64_t size() const;
	/* whether a machine's movie is here (none: nothing to show or edit) */
	bool bound() const;
	/* a frame's buttons, one bit each (the first 64); nothing past the end */
	uint64_t at(int64_t frame) const;
	/* frames first.. into out (as many as out holds) */
	void read(int64_t first, std::vector<uint64_t> &out) const;
	/* sets a frame's buttons, keeping its axes; past the end lengthens the
	 * movie. False with no machine open. */
	bool set(int64_t frame, uint64_t mask);
	void resize(int64_t frames);
	/* empty frames put in at `frame`, or frames taken out from it (the lag
	 * flags move with them) */
	void insert(int64_t frame, int64_t count);
	void remove(int64_t frame, int64_t count);
	/* a whole frame: its buttons and its analog values (nullptr: as they are) */
	bool setRow(int64_t frame, uint64_t mask, const int32_t *axes);
	/* an analog control's value when nothing is held */
	void neutralAxes(int32_t *out) const;
	/* whether the edits made from now on count as rerecords (off for the
	 * rest of a stroke after its first cell) */
	void countRerecords(bool on);
	/* whether the movie counts rerecords at all (Chimera's movie
	 * IsCountingRerecords, a script's movie.setrerecordcounting): on for each
	 * machine bound */
	void setCountingRerecords(bool on);
	bool countingRerecords() const;

	size_t axisCount() const;
	/* out gets axisCount() values, neutral past the end */
	void axesAt(int64_t frame, int32_t *out) const;
	bool setAxesAt(int64_t frame, const int32_t *values);

	/* the whole movie, as text the engine reads back (a branch) */
	std::string text() const;
	/* frames first to last, a line each, as the movie has them */
	std::vector<std::string> lines(int64_t first, int64_t last) const;

	/* true when the frame's lag flag changed */
	bool setLag(int64_t frame, bool lag);
	/* the frame's lag flag not known again (as if never run) */
	bool forgetLag(int64_t frame);
	bool lagAt(int64_t frame) const;
	/* the machine has run the frame (its lag flag is known), now or before
	 * an edit sent the greenzone back past it */
	bool ranAt(int64_t frame) const;
	void clearLags();

	/* the machine's thread: the machine whose movie this is, nullptr for
	 * none - before it frees one */
	void bind(ce_session *s);

private:
	mutable std::shared_mutex m_;
	/* an edit that reads a frame and writes it back with half kept, one at a time */
	std::mutex edit_;
	ce_session *s_ = nullptr;
	/* what countRerecords last asked, and the movie's own switch over it */
	bool strokeCounts_ = true, movieCounts_ = true;
	/* a frame each: 1 lag, 0 not, kNotRun never run (put in since) */
	static constexpr uint8_t kNotRun = 2;
	std::vector<uint8_t> lags_;
};

/* a movie line read from its first '|', as Chimera's MovieController reads
 * one (chimera 10b9668): whitespace and the digits 0-9 before it dropped - the
 * frame number Copy puts there when asked ("00002 |..U.....|"), an indent, a
 * tab; any other line as it is, for its reader to refuse */
std::string movieLineFromBar(const std::string &line);

} // namespace chimera::vm
