/* movie_entry - the Bk2 input-entry format, as a pure function of a
 * controller's declaration.
 *
 * This is movie-correctness data: what "|....U...|" means for a given set of
 * buttons and axes, and what a given machine input generates. It lives apart
 * from the session so it can be tested with byte-exact fixtures against
 * controllers no core in the tree declares - axes especially, which the synth
 * witness core has none of.
 *
 * The layout rule is the one every movie has used: controls grouped by player number (0 = console), axes before buttons within
 * a group, both in declaration order. A control's player is what its package
 * declares ("players", control_names.hpp), else the "P<n> " in front of its
 * name.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace chimera {

struct EntryAxis
{
	std::string name;
	int32_t min = 0, max = 0, neutral = 0;
};

/* "^P(\d+) " captures the player, else 0 */
/* the largest number a control's "P<n> " prefix is read as */
constexpr int32_t kMostPlayers = 255;
int32_t playerNumberOf(const std::string &name);

/* The '|'-delimited groups an entry is written in, and where each control
 * sits inside them. Build once per controller, then parse/generate with it. */
class EntryLayout
{
public:
	/* activeButtons/activeAxes, when given, are one byte per DECLARED control:
	 * zero for a control this machine does not have. A package declares the
	 * union of every peripheral its ports can hold, because the declaration is
	 * static; which of them exist is a question only the running core can
	 * answer, and the answer changes what a movie entry looks like. An entry
	 * carries the controls the machine HAS and no others.
	 *
	 * Item indices stay pointers into the full declared arrays, so everything
	 * downstream still reads and writes at the declared position - only the
	 * text gets shorter. Null means "all of them", which is what a core that
	 * does not answer the question gets. */
	/* playerOf: a control's player by its name (0 the console's); none is
	 * the "P<n> " rule (playerNumberOf) */
	void build(const std::vector<std::string> &buttons, const std::vector<EntryAxis> &axes,
		const std::vector<uint8_t> *activeButtons = nullptr,
		const std::vector<uint8_t> *activeAxes = nullptr,
		const std::function<int32_t(const std::string &)> &playerOf = nullptr);

	/* The columns a movie's LogKey names ("#Reset|Power|#P1 Up|P1 Down|..."),
	 * matched to the machine's controls by NAME, as the window's
	 * MovieControllerDefinition does (user-decided, 2026-10-15): a name the
	 * machine has as an axis is an axis column, any other name is a one-
	 * character button column, and a column the machine lacks is read past
	 * and written as '.'. A control the machine has and the key lacks stays
	 * released or neutral. Reading by position against the machine's own
	 * list instead read every column after a difference as its neighbour.
	 * false (and nothing changed) for a key with no '#' group, or one that
	 * names no control at all. */
	bool buildFromKey(const std::string &key, const std::vector<std::string> &buttons,
		const std::vector<EntryAxis> &axes);

	/* Positional and '|'-tolerant - the exact Bk2Controller.SetFromMnemonic
	 * walk. false when the entry runs out before the controller does, or an
	 * axis field will not parse. buttonsOut is sized to the button count
	 * (one byte per button, 0/1, declaration order - a DOS keyboard is wider
	 * than any packed word); axesOut is sized to the axis count and
	 * pre-filled with each axis's neutral. */
	bool parse(const char *entry, std::vector<uint8_t> &buttonsOut, std::vector<int32_t> &axesOut) const;

	/* mnemonics gives the character a pressed button generates, one per button
	 * in declaration order; a button past the end of the string generates '!'.
	 * buttons carries buttonCount() 0/1 states (null = all released); axes may
	 * be null for all-neutral. */
	std::string generate(const uint8_t *buttons, const int32_t *axes, const std::string &mnemonics) const;

	/* Whether a column of this layout is the machine's button (or axis) at
	 * that declared index. */
	bool hasControl(bool isAxis, int32_t index) const;
	/* The LogKey that names this layout's columns, in the window's form:
	 * '#' before each group, '|' after
	 * each name. Empty when a column has no name the machine knows. */
	std::string key(const std::vector<std::string> &buttons) const;
	/* The names a piece of a LogKey holds, as "P1 A, P1 B", for messages. */
	static std::string namesIn(const std::string &keyPart);

	int32_t groups() const { return groups_; }
	size_t size() const { return items_.size(); }
	size_t buttonCount() const { return buttonCount_; }

private:
	struct Item
	{
		bool isAxis;
		int32_t index; // into buttons / axes; -1 for a key column the machine lacks
	};
	std::vector<Item> items_;
	std::vector<int32_t> groupStarts_;
	std::vector<EntryAxis> axes_;
	int32_t groups_ = 0;
	size_t buttonCount_ = 0;
};

} // namespace chimera
