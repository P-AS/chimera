#pragma once
/* Which core build a game's compiled code belongs to (docs/compile-cache.md,
 * "One directory per game"). A game's folder holds the code one build of a
 * core compiled, and its manifest.json names that build: the core and its
 * package version. Code another build compiled is wrong for this one even
 * where its files have the same names, and a core that read it crashed or hung
 * (issues 361 and 383), so a folder is emptied before a different build uses
 * it, and code no build is named for (files with no manifest) goes the same
 * way. */

#include <optional>
#include <string>

namespace chimera::vm::compiled {

/* a core build: the core's name and its package's version */
struct Build
{
	std::string core, version;
	bool operator==(const Build &) const = default;
};

/* the build a package is (its waterbox.config's coreName and version);
 * nullopt when it does not read */
std::optional<Build> buildOf(const std::string &package);
/* the build a game's folder holds code of, as its manifest names it;
 * nullopt when there is no manifest */
std::optional<Build> ownerOf(const std::string &dir);
/* whether the folder holds no code but this build's */
bool belongsTo(const std::string &dir, const Build &build);
/* Makes the folder this build's before it is used: code of another build,
 * or of none named, is removed, and a folder without a manifest is given
 * one naming this build (no modules: a compile lists them). True when
 * anything was removed. */
bool claim(const std::string &dir, const Build &build);

} // namespace chimera::vm::compiled
