#!/usr/bin/env python3
"""Chimera's Lua documentation, read from its C# libraries, for the Qt
prototype's Lua Functions List.

Chimera builds its list by reflection over the libraries' methods: each
[LuaMethod("name", "description")], its [LuaMethodExample], whether it is
[LuaDeprecatedMethod] or [LuaUnavailableMethod], and the return and
parameter types as LibraryFunction.TypeCleanup writes them ("nluatable",
"int?", "[string domain = nil]"). This reads the same from the source, so
the prototype shows Chimera's own words and stays in step with them.

  lua_docs_from_csharp.py --list <source/gui>          the files it reads
  lua_docs_from_csharp.py <source/gui> <out.inc>       the C++ include
"""

import json
import os
import re
import sys

FOLDERS = ["Chimera.Client.Common/lua", "Chimera.Client.GUI/tools/Lua"]


def sources(root):
    out = []
    for folder in FOLDERS:
        for dirpath, _, names in os.walk(os.path.join(root, folder)):
            for n in sorted(names):
                if n.endswith(".cs"):
                    path = os.path.join(dirpath, n)
                    with open(path, encoding="utf-8-sig") as f:
                        if "[LuaMethod(" in f.read():
                            out.append(path)
    return sorted(out)


def read_string(text, i):
    """A C# string literal at text[i] (regular, verbatim or raw): its value and where it ends."""
    if text.startswith('"""', i):
        # a raw string: its lines between the quotes, without the line breaks
        # after the opening quotes and before the closing ones, and each line
        # without the closing line's indentation, as C# reads one
        j = text.index('"""', i + 3)
        lines = text[i + 3:j].replace("\r", "").split("\n")
        if len(lines) > 1 and lines[0].strip() == "":
            lines = lines[1:]
        indent = ""
        if len(lines) > 1 and lines[-1].strip() == "":
            indent = lines[-1]
            lines = lines[:-1]
        return "\n".join(l[len(indent):] if l.startswith(indent) else l.lstrip() for l in lines), j + 3
    if text.startswith('@"', i):
        j, out = i + 2, []
        while j < len(text):
            if text[j] == '"':
                if text.startswith('""', j):
                    out.append('"')
                    j += 2
                    continue
                return "".join(out), j + 1
            out.append(text[j])
            j += 1
        raise ValueError("unended verbatim string")
    assert text[i] == '"'
    j, out = i + 1, []
    escapes = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'", "0": "\0"}
    while j < len(text):
        c = text[j]
        if c == "\\":
            e = text[j + 1]
            if e == "u":
                out.append(chr(int(text[j + 2:j + 6], 16)))
                j += 6
                continue
            out.append(escapes.get(e, e))
            j += 2
            continue
        if c == '"':
            return "".join(out), j + 1
        out.append(c)
        j += 1
    raise ValueError("unended string")


def attribute_strings(text, start):
    """The string arguments of an attribute whose "(" is at start: each argument's
    literals joined ("a" + "b"), up to the closing ")"."""
    args, cur, i, depth = [], [], start + 1, 0
    while i < len(text):
        c = text[i]
        if c in "\"@" and (c == '"' or text.startswith('@"', i)):
            value, i = read_string(text, i)
            cur.append(value)
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            if depth == 0:
                args.append("".join(cur))
                return args, i + 1
            depth -= 1
        elif c == "," and depth == 0:
            args.append("".join(cur))
            cur = []
        i += 1
    raise ValueError("unended attribute")


# a C# type as LibraryFunction.TypeCleanup writes the type reflection names
SIMPLE = {
    "string": "string", "int": "int", "uint": "uint", "long": "long", "ulong": "ulong", "short": "short",
    "byte": "byte", "bool": "bool", "double": "double", "float": "float", "object": "object", "void": "void",
    "LuaTable": "nluatable", "LuaFunction": "nluafunc", "Color": "color", "byte[]": "string", "object[]": "object[]",
    "bool[]": "bool[]", "ReadOnlyMemory<byte>": "string", "Memory<byte>": "string",
    "int?": "int?", "uint?": "uint?", "bool?": "bool?", "Color?": "color?",
    "ReadOnlyMemory<byte>?": "string?", "Memory<byte>?": "string?",
    "long?": "nullable`1[long ]", "double?": "nullable`1[double ]", "float?": "nullable`1[float ]",
    "ulong?": "nullable`1[ulong ]", "short?": "nullable`1[short ]", "byte?": "nullable`1[byte ]",
}


def type_text(t):
    t = re.sub(r"/\*.*?\*/", "", t)
    t = re.sub(r"\s+", "", t)
    if t in SIMPLE:
        return SIMPLE[t]
    # another type: its name, lowered (a namespace-qualified name has its dots taken out)
    return t.replace(".", "").replace("?", "?").lower()


def default_text(value):
    v = value.strip()
    if v == "null":
        return "nil"
    if v.startswith('"') or v.startswith('@"'):
        return '"' + read_string(v, 0)[0] + '"'
    if v in ("true", "false"):
        return v.capitalize()
    if re.fullmatch(r"-?\d+[uUlLfFdDmM]*", v):
        return re.sub(r"[uUlLfFdDmM]+$", "", v)
    if re.fullmatch(r"-?\d+\.\d+[fFdDmM]?", v):
        n = float(re.sub(r"[fFdDmM]$", "", v))
        return str(int(n)) if n == int(n) else repr(n)
    # an enum's member: its name
    return v.split(".")[-1]


def split_top(text, sep=","):
    parts, depth, cur = [], 0, []
    i = 0
    while i < len(text):
        c = text[i]
        if c in "\"@" and (c == '"' or text.startswith('@"', i)):
            value_start = i
            _, i = read_string(text, i)
            cur.append(text[value_start:i])
            continue
        if c in "<([{":
            depth += 1
        elif c in ">)]}":
            depth -= 1
        if c == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    if "".join(cur).strip():
        parts.append("".join(cur))
    return parts


def parameter_names(params):
    """each parameter's name as written and whether it may be left out (Sublime Text's completions use them)"""
    out = []
    for p in split_top(re.sub(r"/\*.*?\*/", "", params)):
        p = re.sub(r"\[[^\]]*\]\s*", "", p.strip()).strip()
        optional = "=" in p
        p = re.sub(r"^(params|ref|out|in|this)\s+", "", p.split("=", 1)[0].strip())
        m = re.fullmatch(r"(.+?)\s+([A-Za-z_][A-Za-z0-9_]*)", p)
        if m:
            out.append({"name": m.group(2), "optional": optional})
    return out


def parameter_list(params):
    out = []
    for p in split_top(re.sub(r"/\*.*?\*/", "", params)):
        p = p.strip()
        attrs = re.findall(r"\[([A-Za-z]+)[^\]]*\]", p)
        p = re.sub(r"\[[^\]]*\]\s*", "", p).strip()
        default = None
        if "=" in p:
            p, default = p.split("=", 1)
            p = p.strip()
        p = re.sub(r"^(params|ref|out|in|this)\s+", "", p)
        m = re.fullmatch(r"(.+?)\s+([A-Za-z_][A-Za-z0-9_]*)", p)
        if not m:
            continue
        t, name = type_text(m.group(1)), m.group(2).lower()
        if "LuaColorParam" in attrs:
            t = t.replace("object", "luacolor")
        if "LuaZeroIndexed" in attrs:
            t = t.replace("nluatable", "nluatable0Indexed")
        text = t + " " + name
        out.append("[" + text + " = " + default_text(default) + "]" if default is not None else text)
    return "(" + ", ".join(out) + ")"


def without_comments(text):
    """The C# text without its comments and its # lines, its string literals
    untouched: a comment inside an attribute or before a signature hid the
    function from this reader (client.setwindowsize, gui.createcanvas,
    forms.textbox, tastudio.getmarker, mainmemory.getname)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('"""', i):
            j = text.find('"""', i + 3)
            j = n if j < 0 else j + 3
            out.append(text[i:j])
            i = j
            continue
        if text.startswith('@"', i) or text.startswith('$@"', i) or text.startswith('@$"', i):
            k = text.index('"', i) + 1
            while k < n:
                if text[k] == '"':
                    if text.startswith('""', k):
                        k += 2
                        continue
                    k += 1
                    break
                k += 1
            out.append(text[i:k])
            i = k
            continue
        if c in "\"'":
            k = i + 1
            while k < n and text[k] != c:
                k += 2 if text[k] == "\\" else 1
            out.append(text[i:k + 1])
            i = k + 1
            continue
        if text.startswith("//", i):
            k = text.find("\n", i)
            i = n if k < 0 else k
            continue
        if text.startswith("/*", i):
            k = text.find("*/", i + 2)
            out.append(" ")
            i = n if k < 0 else k + 2
            continue
        if c == "#" and text[text.rfind("\n", 0, i) + 1:i].strip() == "":
            k = text.find("\n", i)
            i = n if k < 0 else k
            continue
        out.append(c)
        i += 1
    return "".join(out)


def functions(path):
    with open(path, encoding="utf-8-sig") as f:
        text = without_comments(f.read())
    m = re.search(r'public\s+override\s+string\s+Name\s*=>\s*"([^"]*)"', text)
    cls = re.search(r"(?:public|internal)\s+(?:sealed\s+|static\s+|partial\s+)*class\s+([A-Za-z0-9_]+)", text)
    library = m.group(1) if m else (cls.group(1) if cls else os.path.basename(path)[:-3])
    out = []
    # where the last function's signature ended: the attributes of the next are after it
    previous = 0
    for at in [x.start() for x in re.finditer(r"\[LuaMethod\(", text)]:
        name, description = (attribute_strings(text, text.index("(", at))[0] + ["", ""])[:2]
        # the attributes above it, back to the previous function's signature
        # (an example holds ';' and '}' of its own, so those cannot say where
        # they begin; that function's body has no attributes)
        before = text[previous:at]
        after_start = text.index("]", attribute_strings(text, text.index("(", at))[1]) + 1
        # the attributes after it, and the method's signature
        sig = re.compile(r"\s*(?:\[[^\]]*\]\s*)*public\s+(?:static\s+|virtual\s+|override\s+|new\s+)*([A-Za-z0-9_<>\[\]?.,\s]+?)\s+([A-Za-z0-9_]+)\s*\(")
        s = sig.match(text, after_start)
        if not s:
            continue
        previous = s.end()
        between = text[after_start:s.start(1)]
        attrs = before + between
        example = ""
        e = re.search(r"\[LuaMethodExample\(", attrs)
        if e:
            example = attribute_strings(attrs, attrs.index("(", e.start()))[0][0]
        # the parameters: up to the matching ")"
        i, depth = s.end(), 0
        j = i
        while j < len(text):
            c = text[j]
            if c in "\"@" and (c == '"' or text.startswith('@"', j)):
                _, j = read_string(text, j)
                continue
            if c == "(":
                depth += 1
            elif c == ")":
                if depth == 0:
                    break
                depth -= 1
            j += 1
        returns = type_text(s.group(1))
        if "return: LuaZeroIndexed" in attrs:
            returns = returns.replace("nluatable", "nluatable0Indexed")
        out.append({
            "library": library, "name": name, "returns": returns, "params": parameter_list(text[i:j]),
            "args": parameter_names(text[i:j]),
            "description": description, "example": example,
            "deprecated": "LuaDeprecatedMethod" in attrs, "unavailable": "LuaUnavailableMethod" in attrs,
        })
    return out


def main(argv):
    if len(argv) == 3 and argv[1] == "--list":
        print("\n".join(sources(argv[2])))
        return 0
    root, target = argv[1], argv[2]
    docs = []
    for path in sources(root):
        docs.extend(functions(path))
    body = json.dumps(docs, ensure_ascii=False, separators=(",", ":"))
    assert ")chimeraLuaDocs\"" not in body
    text = ("/* made by lua_docs_from_csharp.py from Chimera's C# Lua libraries: do not edit */\n"
            "static const char kChimeraLuaDocs[] = R\"chimeraLuaDocs(" + body + ")chimeraLuaDocs\";\n")
    old = None
    if os.path.exists(target):
        with open(target, encoding="utf-8") as f:
            old = f.read()
    if old != text:
        with open(target, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
