#!/usr/bin/env python3
"""Writes the README's table of which version of each emulator a core builds.

A core's repository pins its emulator as a git submodule, and its
waterbox/package-licenses.json says which submodule that is: the first
component whose commit is "{submodule:<path>}". Where that is not the
emulator, the core's row of official-cores.json names the submodule's path
("emulator"). This reads them from a folder
of core checkouts - one per row of official-cores.json, named after the core
or its repository - and rewrites the lines of README.md between the two
core-versions markers. Nothing is fetched: what a checkout does not know (a
shallow clone has no tags to name a release by) is left out, not guessed.

usage: tools/core-versions.py <folder of core checkouts> [--check]
       --check: change nothing; exit 1 if the README's table is not this one
"""
import datetime
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BEGIN, END = "<!-- core-versions:begin -->", "<!-- core-versions:end -->"


def git(folder, *args):
    r = subprocess.run(["git", "-C", folder, *args], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def checkout_of(cores_dir, core):
    repo = core["repo"].split("/")[-1]
    short = repo.replace("chimera-core-", "")
    for name in (short, core["id"], repo, short.replace("-", "")):
        folder = os.path.join(cores_dir, name)
        if os.path.isdir(os.path.join(folder, ".git")) or os.path.isfile(os.path.join(folder, ".git")):
            return folder
    return None


def submodules_of(folder):
    """{path: url} as .gitmodules has them, in its order (a section's name is
    not always its path)."""
    listed = git(folder, "config", "-f", ".gitmodules", "--get-regexp", r"^submodule\..*\.(path|url)$")
    by_name = {}
    for line in listed.splitlines():
        key, _, value = line.partition(" ")
        m = re.fullmatch(r"submodule\.(.*)\.(path|url)", key)
        by_name.setdefault(m.group(1), {})[m.group(2)] = value
    return {e["path"]: re.sub(r"\.git$", "", e.get("url", "")) for e in by_name.values() if "path" in e}


def emulator_of(folder, named=None):
    """The emulator's submodule: the one the roster names, else the first the
    package's licence list names, else the first there is."""
    subs = submodules_of(folder)
    if named is not None:
        if named not in subs:
            raise SystemExit("%s has no submodule at %s" % (folder, named))
        return named, subs[named]
    try:
        components = json.load(open(os.path.join(folder, "waterbox", "package-licenses.json")))["components"]
    except (OSError, ValueError, KeyError):
        components = []
    for c in components:
        m = re.fullmatch(r"\{submodule:(.+)\}", str(c.get("commit", "")))
        if m and m.group(1) in subs:
            return m.group(1), subs[m.group(1)]
    for path, url in subs.items():
        return path, url
    return None


def release_of(folder, sha):
    """The nearest release tag behind the commit, when the checkout has one
    that reads as a version (a moving tag like "nightly" names nothing)."""
    described = git(folder, "describe", "--tags", "--long", sha)
    m = re.fullmatch(r"(.+)-(\d+)-g[0-9a-f]+", described)
    if not m or not re.search(r"\d+\.\d+", m.group(1)):
        return ""
    tag, ahead = m.group(1), int(m.group(2))
    return tag if ahead == 0 else "%s + %d commits" % (tag, ahead)


def row(cores_dir, core):
    link = "[%s](https://github.com/%s)" % (core["name"], core["repo"])
    folder = checkout_of(cores_dir, core)
    if folder is None:
        raise SystemExit("no checkout of %s in %s" % (core["repo"], cores_dir))
    emulator = emulator_of(folder, core.get("emulator"))
    if emulator is None:
        return "| %s | in the core's own repository | | | |" % link
    path, url = emulator
    sha = git(folder, "rev-parse", "HEAD:" + path)
    sub = os.path.join(folder, path)
    date = git(sub, "log", "-1", "--format=%cs", sha)
    source = "[%s](%s)" % (re.sub(r"^https://github\.com/", "", url), url) if url else path
    commit = "[`%s`](%s/commit/%s)" % (sha[:9], url, sha) if url.startswith("https://github.com/") else "`%s`" % sha[:9]
    return "| %s | %s | %s | %s | %s |" % (link, source, commit, date, release_of(sub, sha))


def main():
    args = [a for a in sys.argv[1:] if a != "--check"]
    if len(args) != 1:
        raise SystemExit(__doc__)
    cores = json.load(open(os.path.join(ROOT, "official-cores.json")))["cores"]
    lines = [
        BEGIN,
        "| Core | Emulator source | Commit | Its date | Nearest release |",
        "| --- | --- | --- | --- | --- |",
    ] + [row(args[0], c) for c in cores] + [END]
    path = os.path.join(ROOT, "README.md")
    text = open(path).read()
    if BEGIN not in text or END not in text:
        raise SystemExit("README.md has no core-versions markers")
    head, rest = text.split(BEGIN, 1)
    tail = rest.split(END, 1)[1]
    new = head + "\n".join(lines) + tail
    if "--check" in sys.argv:
        sys.exit(0 if new == text else 1)
    open(path, "w").write(new)
    print("README.md: %d cores, %s" % (len(cores), datetime.date.today().isoformat()))


if __name__ == "__main__":
    main()
