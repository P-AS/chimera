#pragma once
/* The names Lua's client.getconfig gives the window's settings, each tied to
 * the window's setting it reads, and how the value is shown to a script. */

#include <string>
#include <vector>

namespace chimera::vm {

struct ConfigName
{
	enum class Kind
	{
		/* the same value both sides */
		Plain,
		/* a number for the script, named from `extra` ("None|System|..."
		 * for 0, 1, ...) */
		Choice,
		/* New Project's list: 1 and 2 for the window's "emulators" and "games" */
		CoreKind,
		/* a signed 32-bit ARGB number for the window's "#AARRGGBB" */
		Colour,
		/* megabytes for the window's gigabytes */
		Megabytes,
		/* one of three throttle switches from the window's one choice
		 * (`key`) of which, `extra` naming this one's ("clock") */
		Throttle,
		/* a width or height, 0 for no resize, from the window's resize
		 * switch (`key`) and the size (`extra`, "encoder.width") */
		Resize,
		/* 0 nothing, 1 minimal, 2 full, from the window's Display Nothing
		 * (`key`) and Minimal (`extra`) */
		DisplayFeatures,
		/* nil for the window's "" */
		NullWhenEmpty,
		/* the place of one of what is drawn over the picture, {X, Y, Anchor},
		 * from one entry of the window's positions (`key`) by its name there
		 * (`extra`, "Frame Counter") */
		Place,
	};
	/* the script's name; "Movies.GreenzoneBudgetMb" for one inside Movies */
	const char *name;
	Kind kind;
	/* the window's setting, "session.speedPercent" */
	const char *key;
	const char *extra;
};

/* every setting a script can read */
const std::vector<ConfigName> &configNames();

/* the list for Lua: a line each, name, kind, key and extra between tabs */
std::string configNamesText();

} // namespace chimera::vm
