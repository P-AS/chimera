#pragma once
/* Chimera's settings (its config.ini, a JSON object) by Chimera's names, each
 * tied to the Qt window's setting that means the same. One list serves both
 * ways: Lua's client.getconfig reads the window's settings under Chimera's
 * names, and Chimera's file is read once into the window's settings when the
 * Qt window takes Chimera's place (owner, 2026-10-26). */

#include <string>
#include <vector>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

struct ChimeraSetting
{
	enum class Kind
	{
		/* the same value both sides */
		Plain,
		/* a number both sides; Lua gives Chimera's name for it, from `extra`
		 * ("None|System|..." for 0, 1, ...) */
		Choice,
		/* New Project's list: Chimera's 1 and 2 are the window's "emulators" and "games" */
		CoreKind,
		/* Chimera's signed 32-bit ARGB number; the window's "#AARRGGBB" */
		Colour,
		/* Chimera's megabytes; the window's gigabytes */
		Megabytes,
		/* one of Chimera's three throttle switches; the window's one choice
		 * (`key`) of which, `extra` naming this one's ("clock") */
		Throttle,
		/* Chimera's width or height, 0 for no resize; the window's resize
		 * switch (`key`) and the size (`extra`, "encoder.width") */
		Resize,
		/* Chimera's Display Features, 0 nothing, 1 minimal, 2 full; the
		 * window's Display Nothing (`key`) and Minimal (`extra`) */
		DisplayFeatures,
		/* Chimera's null; the window's "" */
		NullWhenEmpty,
		/* one of the places of what is drawn over the picture, Chimera's
		 * {X, Y, Anchor}; one entry of the window's positions (`key`) by its
		 * name there (`extra`, "Frame Counter") */
		Place,
	};
	/* Chimera's name; "Movies.GreenzoneBudgetMb" for one inside Movies */
	const char *name;
	Kind kind;
	/* the window's setting, "session.speedPercent" */
	const char *key;
	const char *extra;
};

/* every setting the two have in common */
const std::vector<ChimeraSetting> &chimeraSettings();

/* the list for Lua: a line each, name, kind, key and extra between tabs */
std::string chimeraSettingsText();

/* whether a settings file is Chimera's (its own names) rather than the
 * window's ("session.speedPercent") */
bool isChimeraSettings(const json::Json &root);

struct ChimeraImport
{
	/* the window's settings, as its own file holds them */
	json::Json settings;
	/* Chimera's names carried over, and those it has that are not */
	std::vector<std::string> carried, left;
};

/* Chimera's settings read as the window's; false with the reason when the
 * text is not Chimera's settings */
bool importChimeraSettings(const std::string &text, ChimeraImport &out, std::string &error);

} // namespace chimera::vm
