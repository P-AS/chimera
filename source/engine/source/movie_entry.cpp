/* movie_entry.cpp - see movie_entry.hpp. Every movie ever written must parse
 * and regenerate byte for byte, so the walk is kept literal rather than
 * tidied.
 */

#include "movie_entry.hpp"

#include <algorithm>
#include <cstring>

namespace chimera {

namespace {

/* int parse for an axis field: optional spaces (the padding), sign, digits */
bool parseAxisValue(const char *begin, const char *end, int32_t &out)
{
	while (begin < end && *begin == ' ') begin++;
	if (begin >= end) return false;
	bool negative = false;
	if (*begin == '+' || *begin == '-')
	{
		negative = *begin == '-';
		if (++begin >= end) return false;
	}
	int64_t value = 0;
	for (; begin < end; begin++)
	{
		if (*begin < '0' || *begin > '9') return false;
		value = value * 10 + (*begin - '0');
		if (value > (negative ? 2147483648LL : 2147483647LL)) return false;
	}
	out = static_cast<int32_t>(negative ? -value : value);
	return true;
}

} // namespace

int32_t playerNumberOf(const std::string &name)
{
	if (name.size() < 3 || name[0] != 'P') return 0;
	size_t i = 1;
	int32_t value = 0;
	while (i < name.size() && name[i] >= '0' && name[i] <= '9')
	{
		/* past kMostPlayers it is not a player's number: "P1000000000 A"
		 * would be a billion groups, and longer would not fit in an int */
		if (value > kMostPlayers) return 0;
		value = value * 10 + (name[i] - '0');
		i++;
	}
	return i > 1 && i < name.size() && name[i] == ' ' && value <= kMostPlayers ? value : 0;
}

void EntryLayout::build(const std::vector<std::string> &buttons, const std::vector<EntryAxis> &axes,
	const std::vector<uint8_t> *activeButtons, const std::vector<uint8_t> *activeAxes,
	const std::function<int32_t(const std::string &)> &playerOf)
{
	auto player = [&](const std::string &name) { return playerOf ? playerOf(name) : playerNumberOf(name); };
	auto buttonLive = [&](size_t i) {
		return activeButtons == nullptr || i >= activeButtons->size() || (*activeButtons)[i] != 0;
	};
	auto axisLive = [&](size_t i) {
		return activeAxes == nullptr || i >= activeAxes->size() || (*activeAxes)[i] != 0;
	};

	items_.clear();
	groupStarts_.clear();
	axes_ = axes;
	buttonCount_ = buttons.size();

	/* The group count follows the controls that EXIST. A machine with nothing
	 * in port two has no player-two group, and an entry with an empty group in
	 * it would be a group a person could put nothing in. */
	int32_t maxPlayer = 0;
	for (size_t i = 0; i < axes.size(); i++)
		if (axisLive(i)) maxPlayer = std::max(maxPlayer, player(axes[i].name));
	for (size_t i = 0; i < buttons.size(); i++)
		if (buttonLive(i)) maxPlayer = std::max(maxPlayer, player(buttons[i]));
	groups_ = maxPlayer + 1;
	for (int32_t g = 0; g < groups_; g++)
	{
		groupStarts_.push_back(static_cast<int32_t>(items_.size()));
		for (int32_t i = 0; i < static_cast<int32_t>(axes.size()); i++)
		{
			if (axisLive(static_cast<size_t>(i))
				&& player(axes[static_cast<size_t>(i)].name) == g)
				items_.push_back({ true, i });
		}
		for (int32_t i = 0; i < static_cast<int32_t>(buttons.size()); i++)
		{
			if (buttonLive(static_cast<size_t>(i))
				&& player(buttons[static_cast<size_t>(i)]) == g)
				items_.push_back({ false, i });
		}
	}
}

bool EntryLayout::buildFromKey(const std::string &key, const std::vector<std::string> &buttons,
	const std::vector<EntryAxis> &axes)
{
	const size_t first = key.find('#');
	if (first == std::string::npos) return false;
	/* '#' starts a group and '|' ends a name, but a control's own name may
	 * hold either: Virtual Jaguar's "P2 #", MAME's "| @" and "# >". Splitting
	 * at every '#' made "P2 #" two names and a group too many, and every
	 * column after it was read as another (chimera-core-virtualjaguar's
	 * engine:jag). So at each place the machine's own names come first, the
	 * longest of them, when one is there whole and a '|' or the key's end
	 * follows it; otherwise a '#' starts a group and a name runs to the next
	 * '|'. The window reads a key the same way (MovieController). */
	std::vector<const std::string *> known;
	for (const auto &b : buttons) known.push_back(&b);
	for (const auto &a : axes) known.push_back(&a.name);
	std::stable_sort(known.begin(), known.end(),
		[](const std::string *x, const std::string *y) { return x->size() > y->size(); });
	auto nameAt = [&](size_t at) -> const std::string * {
		for (const std::string *n : known)
		{
			if (n->empty() || key.compare(at, n->size(), *n) != 0) continue;
			const size_t end = at + n->size();
			if (end == key.size() || key[end] == '|') return n;
		}
		return nullptr;
	};
	std::vector<Item> items;
	std::vector<int32_t> starts;
	/* everything before the first '#' is ignored, as the window does */
	size_t at = first;
	bool inGroup = false;
	while (at < key.size())
	{
		const std::string *whole = inGroup ? nameAt(at) : nullptr;
		if (whole == nullptr && key[at] == '#')
		{
			starts.push_back(static_cast<int32_t>(items.size()));
			inGroup = true;
			at++;
			continue;
		}
		std::string name;
		if (whole != nullptr)
		{
			name = *whole;
			at += whole->size();
		}
		else
		{
			const size_t bar = key.find('|', at);
			const size_t end = bar == std::string::npos ? key.size() : bar;
			name = key.substr(at, end - at);
			at = end;
		}
		if (at < key.size() && key[at] == '|') at++;
		if (name.empty()) continue;
		int32_t index = -1;
		bool isAxis = false;
		for (size_t i = 0; i < axes.size() && index < 0; i++)
			if (axes[i].name == name) { index = static_cast<int32_t>(i); isAxis = true; }
		for (size_t i = 0; i < buttons.size() && index < 0; i++)
			if (buttons[i] == name) index = static_cast<int32_t>(i);
		items.push_back({ isAxis, index });
	}
	/* a key that names no control ("#", which a hand-made movie may carry
	 * to say nothing) says nothing about the columns: read by the machine's
	 * order, as a movie with no key is */
	if (items.empty()) return false;
	items_ = std::move(items);
	groupStarts_ = std::move(starts);
	groups_ = static_cast<int32_t>(groupStarts_.size());
	axes_ = axes;
	buttonCount_ = buttons.size();
	return true;
}

bool EntryLayout::parse(const char *entry, std::vector<uint8_t> &buttonsOut, std::vector<int32_t> &axesOut) const
{
	buttonsOut.assign(buttonCount_, 0);
	axesOut.assign(axes_.size(), 0);
	for (size_t i = 0; i < axes_.size(); i++) axesOut[i] = axes_[i].neutral;
	const char *p = entry;
	for (const auto &item : items_)
	{
		while (*p == '|') p++;
		if (*p == '\0') return false; // entry shorter than the controller
		if (item.isAxis)
		{
			const char *comma = std::strchr(p, ',');
			if (comma == nullptr) return false;
			int32_t value;
			if (!parseAxisValue(p, comma, value)) return false;
			axesOut[static_cast<size_t>(item.index)] = value;
			p = comma + 1;
		}
		else
		{
			if (*p != '.' && item.index >= 0) buttonsOut[static_cast<size_t>(item.index)] = 1;
			p++;
		}
	}
	return true;
}

bool EntryLayout::hasControl(bool isAxis, int32_t index) const
{
	for (const Item &item : items_)
	{
		if (item.isAxis == isAxis && item.index == index) return true;
	}
	return false;
}

std::string EntryLayout::key(const std::vector<std::string> &buttons) const
{
	std::string out;
	size_t next = 0;
	for (int32_t g = 0; g < groups_; g++)
	{
		const size_t end = g + 1 < groups_
			? static_cast<size_t>(groupStarts_[static_cast<size_t>(g) + 1])
			: items_.size();
		out.push_back('#');
		for (; next < end; next++)
		{
			const Item &item = items_[next];
			if (item.index < 0) return std::string();
			const size_t at = static_cast<size_t>(item.index);
			if (item.isAxis ? at >= axes_.size() : at >= buttons.size()) return std::string();
			out.append(item.isAxis ? axes_[at].name : buttons[at]).push_back('|');
		}
	}
	return out;
}

std::string EntryLayout::namesIn(const std::string &keyPart)
{
	std::string out, name;
	for (const char c : keyPart + "|")
	{
		if (c != '|' && c != '#') { name.push_back(c); continue; }
		if (name.empty()) continue;
		if (!out.empty()) out += ", ";
		out += name;
		name.clear();
	}
	return out;
}

std::string EntryLayout::generate(const uint8_t *buttons, const int32_t *axes, const std::string &mnemonics) const
{
	std::string out;
	out.push_back('|');
	size_t next = 0;
	for (int32_t g = 0; g < groups_; g++)
	{
		size_t end = g + 1 < groups_
			? static_cast<size_t>(groupStarts_[static_cast<size_t>(g) + 1])
			: items_.size();
		for (; next < end; next++)
		{
			const auto &item = items_[next];
			if (item.isAxis)
			{
				int32_t value = axes != nullptr ? axes[item.index] : axes_[static_cast<size_t>(item.index)].neutral;
				std::string text = std::to_string(value);
				while (text.size() < 5) text.insert(text.begin(), ' '); // PadLeft(5)
				out.append(text).push_back(',');
			}
			else if (item.index < 0)
			{
				out.push_back('.'); /* a key column the machine does not have */
			}
			else
			{
				char mnemonic = static_cast<size_t>(item.index) < mnemonics.size()
					? mnemonics[static_cast<size_t>(item.index)]
					: '!';
				bool pressed = buttons != nullptr && buttons[static_cast<size_t>(item.index)] != 0;
				out.push_back(pressed ? mnemonic : '.');
			}
		}
		out.push_back('|');
	}
	return out;
}

} // namespace chimera
