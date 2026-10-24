#!/bin/bash
# Publishes one built core package as a GitHub release of the core's OWN
# repository. Called by .github/workflows/publish-core.yml, which every core's
# CI reuses - the logic lives here, in one file, rather than as fifteen copies
# of the same YAML drifting apart.
#
# Two kinds of release, the same two the frontend publishes (see release.yml):
#
#   dev      a ROLLING prerelease, replaced on every green push to main.
#   nightly  an IMMUTABLE dated release (the newest is GitHub's Latest), published only
#            when main moved. NEVER deleted: a movie cites the core build that
#            recorded it, and someone replaying that run in five years needs
#            THAT package, because a package's identity is a function of its
#            toolchain and the same sources through a later gcc are different
#            bytes.
#
# The asset is named <core-id>-<version>.chimeraCore, and the version is read
# OUT OF THE PACKAGE rather than passed in: it is what the build stamped into
# waterbox.config, it is what a movie cites, and it is what the core manager
# checks the download against. Anything else is a way for the release and the
# package to disagree.
#
# Usage: publish-core.sh --package <file> --core-id <id> [--kind dev|nightly]
#                        [--sha <commit>] [--notes <file>] [--dry-run]
set -eu

package=""
core_id=""
kind=dev
sha=""
notes=""
dry=0
while [ $# -gt 0 ]; do
	case "$1" in
		--package) package="$2"; shift 2 ;;
		--core-id) core_id="$2"; shift 2 ;;
		--kind) kind="$2"; shift 2 ;;
		--sha) sha="$2"; shift 2 ;;
		--notes) notes="$2"; shift 2 ;;
		--dry-run) dry=1; shift ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ -n "$package" ] && [ -n "$core_id" ] || {
	echo "usage: publish-core.sh --package <file> --core-id <id> [--kind dev|nightly]" >&2; exit 2; }
[ -f "$package" ] || { echo "no package at $package" >&2; exit 1; }
case "$kind" in
	dev|nightly) ;;
	*) echo "kind must be dev or nightly" >&2; exit 2 ;;
esac

# ---- what this package says it is -------------------------------------------
version="$(python3 - "$package" <<'PY'
import json, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    print(json.loads(z.read("waterbox.config").decode("utf-8")).get("version", ""))
PY
)"
# When the commit was made, as the package states it (versionDate, UTC), on
# the clock Chimera shows it in (issue 331): "2026-10-18 18:35 CEST". The
# release says it the same way, so the version a person sees in Chimera can
# be found on the release page. Empty for a package that states no date.
version_date="$(python3 - "$package" <<'PY'
import json, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    print(json.loads(z.read("waterbox.config").decode("utf-8")).get("versionDate", ""))
PY
)"
made=""
[ -z "$version_date" ] || made="$(python3 "$(dirname "$0")/european-time.py" "$version_date")"
if [ -z "$version" ]; then
	echo "the package stamps no version; the build must set CORE_VERSION" >&2
	exit 1
fi
case "$version" in
	*+local*|*-dirty*)
		# a hand-built package is nobody else's build: publishing one would put a
		# version nothing can reproduce into somebody's movie header
		echo "refusing to publish a hand-built package (version $version)" >&2
		exit 1 ;;
esac

asset="$core_id-$version.chimeraCore"
[ -n "$sha" ] || sha="$(git rev-parse HEAD)"
date="$(date -u +%Y-%m-%d)"
if [ "$kind" = dev ]; then
	title="Development build ${version:0:8}${made:+ (made $made)}"
else
	title="Nightly $date (${version:0:8}${made:+, made $made})"
fi

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT
cp "$package" "$staging/$asset"

# What changed since the last nightly (issue 357): the commits after that
# nightly's tag up to this build's, from the core's own history, which the
# workflow checks out whole. Today's nightly tag is passed over - this may be
# the run finishing it. Nothing is said when the history cannot be read.
repo_slug="${GITHUB_REPOSITORY:-}"
[ -n "$repo_slug" ] || repo_slug="$(gh repo view --json nameWithOwner --jq .nameWithOwner 2>/dev/null || true)"
previous=""
for t in $(git tag -l 'nightly-*' --sort=-creatordate 2>/dev/null); do
	[ "$t" = "nightly-$date" ] && continue
	previous="$t"
	break
done
changes=""
if [ -n "$repo_slug" ] && git rev-parse --git-dir >/dev/null 2>&1; then
	changes="$(python3 "$(dirname "$0")/release-changes.py" --repo "$repo_slug" --since "$previous" --to "$sha" --kind "$kind" 2>/dev/null || true)"
fi

if [ -z "$notes" ]; then
	notes="$staging/NOTES.md"
	{
		echo "Core package for [Chimera](https://github.com/ToolAssisted-run/chimera)."
		echo
		echo "Download the \`.chimeraCore\` file below and put it in Chimera's \`Cores\`"
		echo "folder (**File > Core Manager** shows where that is). Chimera downloads"
		echo "nothing itself."
		echo
		echo "| | |"
		echo "|---|---|"
		echo "| Core | \`$core_id\` |"
		echo "| Version | \`$version\` |"
		[ -z "$made" ] || echo "| Made | $made (Chimera lists it as \`$made  (${version:0:8})\`) |"
		echo "| Built from | \`$sha\` |"
		echo
		if [ "$kind" = dev ]; then
			echo "This is a **development build**: it is replaced on every change, so it"
			echo "may stop being downloadable. For a build that is kept permanently -"
			echo "which is what a movie needs to stay replayable - take a nightly."
		else
			echo "This is a **nightly**: it is never deleted. A movie recorded on this"
			echo "package can be replayed against it for as long as this repository"
			echo "exists."
		fi
		if [ -n "$changes" ]; then
			echo
			printf '%s\n' "$changes"
		fi
	} > "$notes"
fi

echo "publishing $asset ($kind) from $sha"
if [ "$dry" -eq 1 ]; then
	echo "--dry-run: would publish \"$title\""
	sed 's/^/  | /' "$notes"
	exit 0
fi

# GitHub's API answers 5xx now and then, and a publish is two calls - create the
# release, then upload into it - so one bad answer between them used to leave a
# release with nothing in it. That is worse than no release: the nightly rule
# below then refused to touch it again, and every consumer that picks "the
# newest nightly" found an empty one (2026-09-13: flycast got a 500 on the
# upload, ares a 502 on a create that had in fact succeeded, and the frontend's
# CI could not download either). So every call is retried, and a release is
# ENSURED rather than created: a create whose answer was lost may still have
# happened, which is why it looks before it tries again.
retry() {
	local n
	for n in 1 2 3 4; do
		"$@" && return 0
		[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: $1 $2 $3" >&2; sleep $((n * 15)); }
	done
	return 1
}
# A dated nightly is a full release and the one GitHub shows as Latest; the
# rolling dev build is a pre-release and never Latest (user-decided,
# 2026-09-25). The index release is made elsewhere and stays a pre-release.
release_kind_flags() {
	case "$1" in
		nightly-*) echo "--latest" ;;
		*) echo "--prerelease --latest=false" ;;
	esac
}

ensure_release() { # <tag> <title>
	local n
	for n in 1 2 3 4; do
		gh release view "$1" >/dev/null 2>&1 && return 0
		# shellcheck disable=SC2046
		gh release create "$1" --target "$sha" $(release_kind_flags "$1") --title "$2" --notes-file "$notes" && return 0
		[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: release $1" >&2; sleep $((n * 15)); }
	done
	return 1
}

if [ "$kind" = dev ]; then
	# ONE TAG, MOVED - through the releases API, never `git push`. A workflow's
	# token may not create or update workflow FILES, and pushing a tag at a
	# commit whose .github/workflows differ from the default branch's counts as
	# exactly that. Deleting the release with its tag and recreating it goes
	# through the API, which has no such restriction.
	#
	# The delete is retried and then checked: a failed one left the old release
	# standing, ensure_release found it and the new package went in beside the
	# old one, under the old title, notes and commit. "Not found" is the only
	# answer that means gone; any other failure to look is asked again.
	dev_gone=0
	for n in 1 2 3 4; do
		gh release delete dev --yes --cleanup-tag >/dev/null 2>&1 || true
		if look=$(gh release view dev 2>&1 >/dev/null); then
			:
		else
			case "$look" in *"not found"*) dev_gone=1; break ;; esac
		fi
		[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: delete the old dev release" >&2; sleep $((n * 15)); }
	done
	if [ "$dev_gone" -ne 1 ]; then
		echo "publish-core: the old dev release could not be deleted; the new package is not put into it" >&2
		exit 1
	fi
	ensure_release dev "$title"
	retry gh release upload dev "$staging/$asset" --clobber
else
	tag="nightly-$date"
	# A nightly that already carries a package of this core is never
	# republished: somebody's movie may name it. One that exists with NO package
	# is a publish that died half way, and finishing it is the only way it ever
	# becomes what its name promises.
	if gh release view "$tag" --json assets --jq '.assets[].name' 2>/dev/null | grep -q "^$core_id-.*\.chimeraCore$"; then
		echo "$tag already exists; a nightly is never republished"
		exit 0
	fi
	ensure_release "$tag" "$title"
	retry gh release upload "$tag" "$staging/$asset" --clobber
fi
echo "published $asset"
