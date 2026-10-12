#!/usr/bin/env python3
"""Plays the part of an AI agent's program for the MCP server's test
(docs/mcp-server.md): starts chimera-mcp, speaks MCP to it on its standard
input and output, and checks what the tools say about the synthetic core
against files the other tests already trust.

Usage: mcp-agent.py <chimera-mcp> <reference ram> <reference vram> <movie.txt> <last frame .tga> [mode]

A running Chimera must be serving. tests/synth/run-witness.sh starts one of
two: chimera-run --mcp-serve, which has no window, or the real window with
--mcp-server and a project ("window" as the last argument). The window is
asked the same things and a few more that only it can answer; it writes no
picture file to compare with, so "-" is given for the .tga and the picture is
only checked to be one.

With "runner-changes" or "window-changes" as the mode the server was started
with changes allowed, and after everything above the tools that change things
are tried: memory and named values are written and read back, and in the
window the input is edited and undone, frames are inserted and deleted, a
marker and branches are made, the playback position is moved - and at the end
the machine, played again to the movie's last frame, must hold the reference
memory, which it only can if every edit was undone exactly.

Prints one line for each thing that is wrong and exits 1, or prints nothing
and exits 0.
"""
import base64
import json
import struct
import subprocess
import sys
import zlib

mcp, ram_path, vram_path, movie_path, tga_path = sys.argv[1:6]
mode = sys.argv[6] if len(sys.argv) > 6 else "runner"
window = mode.startswith("window")
changes = mode.endswith("-changes")
wrong = []


def check(what, ok):
    if not ok:
        wrong.append(what)


agent = subprocess.Popen([mcp], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
next_id = 0


def rpc(method, params=None):
    global next_id
    next_id += 1
    agent.stdin.write(json.dumps({"jsonrpc": "2.0", "id": next_id, "method": method, "params": params or {}}) + "\n")
    agent.stdin.flush()
    reply = json.loads(agent.stdout.readline())
    check("the reply to %s carries its id" % method, reply.get("id") == next_id)
    return reply


def call(tool, **arguments):
    """The tool's answer: (is it an error, its texts joined, its content list)."""
    result = rpc("tools/call", {"name": tool, "arguments": arguments})["result"]
    texts = [c["text"] for c in result["content"] if c["type"] == "text"]
    return result["isError"], "\n".join(texts), result["content"]


def facts(tool, **arguments):
    """The tool's answer as the JSON object its text holds, or {} for an error."""
    error, text, _ = call(tool, **arguments)
    check("%s %s is answered: %s" % (tool, arguments, text[:120]), not error)
    return {} if error else json.loads(text)


def png_pixels(data):
    """(width, height, RGB bytes) of an 8-bit RGB PNG without interlacing."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    at, width, height, packed = 8, 0, 0, b""
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and colour == 2
        elif kind == b"IDAT":
            packed += body
        at += 12 + length
    raw = zlib.decompress(packed)
    stride = width * 3
    out = bytearray()
    previous = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - 3] if i >= 3 else 0
            up = previous[i]
            upleft = previous[i - 3] if i >= 3 else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - upleft
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else upleft)) & 255
        out += line
        previous = line
    return width, height, bytes(out)


def tga_pixels(path):
    """(width, height, RGB bytes, top row first) of an uncompressed 32-bit TGA."""
    data = open(path, "rb").read()
    width, height, depth, descriptor = struct.unpack("<HHBB", data[12:18])
    assert data[2] == 2 and depth == 32
    body = data[18 + data[0]:]
    rows = []
    for y in range(height):
        row = body[y * width * 4:(y + 1) * width * 4]
        rows.append(b"".join(bytes((row[i + 2], row[i + 1], row[i])) for i in range(0, len(row), 4)))
    if not descriptor & 0x20:
        rows.reverse()
    return width, height, b"".join(rows)


# the introduction and the list
hello = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                           "clientInfo": {"name": "the witness", "version": "1"}})["result"]
check("the server names itself", hello["serverInfo"]["name"] == "chimera")
agent.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
agent.stdin.flush()
tools = {t["name"]: t for t in rpc("tools/list")["result"]["tools"]}
for needed in ("status", "controls", "get_input", "screenshot", "list_memory", "read_memory",
               "registers", "list_properties", "get_property"):
    check("the tool %s is listed" % needed, needed in tools)

movie = [line.rstrip("\r\n") for line in open(movie_path) if line.startswith("|")]
ram = open(ram_path, "rb").read()
vram = open(vram_path, "rb").read()

# status: the machine the run left
status = facts("status")
check("status names the core", status.get("core") == "Synth")
check("status is on the movie's last frame", status.get("frame") == len(movie))
check("status gives the movie's length", status.get("movie_length") == len(movie))
check("status says whether changes are allowed", status.get("changes_allowed") is changes)
check("status says the core has not stopped", status.get("core_stopped") is False)
check("status gives the picture's size", status.get("picture", {}).get("width", 0) > 0)
if window:
    check("status names the project", status.get("project") == "gridWalker.win")
    check("status says the window is paused", status.get("paused") is True)
    check("status says nothing is unsaved", status.get("unsaved_changes") is False)

# controls: the buttons, in the order of the movie's columns
controls = facts("controls")
names = [b["name"] for b in controls.get("buttons", [])]
check("controls lists the buttons in order", names[:4] == ["P1 Up", "P1 Down", "P1 Left", "P1 Right"])

# the input: every line, read in two calls, is the movie file's
got = facts("get_input", count=40)["entries"] + facts("get_input", **{"from": 40, "count": 1000})["entries"]
check("get_input returns the movie's lines", got == movie)
check("get_input past the end returns nothing", facts("get_input", **{"from": len(movie) + 5})["entries"] == [])
error, text, _ = call("get_input", count=5000)
check("get_input refuses more than 1000 frames", error and "1000" in text)

# memory: both domains, byte for byte, are the reference dumps
memory = facts("list_memory")
sizes = {d["name"]: d["size"] for d in memory.get("domains", [])}
check("list_memory gives RAM and VRAM their sizes", sizes.get("RAM") == len(ram) and sizes.get("VRAM") == len(vram))
for name, reference in (("RAM", ram), ("VRAM", vram)):
    read = b""
    for address in range(0, len(reference), 4096):
        count = min(4096, len(reference) - address)
        read += bytes.fromhex(facts("read_memory", domain=name, address=address, count=count).get("bytes_hex", ""))
    check("read_memory of %s is the reference dump" % name, read == reference)
wide = facts("read_memory", domain="RAM", address=2, count=3, type="u16")
check("read_memory reads 16-bit values, low byte first",
      wide.get("values") == [ram[2 + 2 * i] | ram[3 + 2 * i] << 8 for i in range(3)])
wide = facts("read_memory", domain="RAM", address=2, count=2, type="s32", big_endian=True)
check("read_memory reads signed 32-bit values, high byte first",
      wide.get("values") == [struct.unpack(">i", ram[2 + 4 * i:6 + 4 * i])[0] for i in range(2)])
error, text, _ = call("read_memory", domain="NO SUCH", address=0)
check("read_memory names a domain that does not exist", error and "NO SUCH" in text)
error, text, _ = call("read_memory", domain="RAM", address=len(ram) - 2, count=8)
check("read_memory refuses a read past the end", error and "past its end" in text)
error, text, _ = call("read_memory", domain="RAM", address=0, count=5000)
check("read_memory refuses more than 4096 bytes", error and "4096" in text)

# the game's named values: the movie wins the game
properties = facts("list_properties", filter="stat")
check("list_properties filters by name", [p["name"] for p in properties.get("properties", [])] == ["Status"])
check("get_property reads a value as a person does", facts("get_property", name="Status").get("value") == "Won")
error, text, _ = call("get_property", name="No Such Value")
check("get_property names a value that does not exist", error and "No Such Value" in text)

# the picture: the same pixels chimera-run wrote for the last frame
error, text, content = call("screenshot")
images = [c for c in content if c["type"] == "image"]
check("screenshot returns a PNG", not error and len(images) == 1 and images[0]["mimeType"] == "image/png")
if images:
    width, height, pixels = png_pixels(base64.b64decode(images[0]["data"]))
    check("screenshot has the picture's size", (width, height) == (status["picture"]["width"], status["picture"]["height"]))
    check("screenshot is not one flat colour", len(set(pixels[i:i + 3] for i in range(0, len(pixels), 3))) > 1)
    if tga_path != "-":
        check("screenshot is the last frame's picture", (width, height, pixels) == tga_pixels(tga_path))
    half = facts("screenshot", max_width=width // 2)
    check("screenshot scales a picture down to max_width", half.get("width") == width // 2 and half.get("picture_width") == width)

if window:
    # the tools the window runs: what TAStudio holds
    markers = facts("list_markers").get("markers")
    check("list_markers lists the project's markers, the first on frame 0",
          isinstance(markers, list) and len(markers) > 0 and markers[0].get("frame") == 0 and "text" in markers[0])
    check("list_branches lists the project's branches, of which there are none", facts("list_branches").get("branches") == [])
else:
    # a tool that needs the window says so here, where there is none
    error, text, _ = call("list_markers")
    check("a tool the frontend runs is handed to the frontend", error and "window" in text)

# RAM search: looking only, so it works whether or not changes are allowed. What is left
# after a filter must be what the reference dump says is left.
error, text, _ = call("search_results")
check("search_results says that no search is running", error and "search_start" in text)
check("search_start makes every byte a candidate", facts("search_start", domain="RAM").get("candidates") == len(ram))
same = [i for i in range(len(ram)) if ram[i] == ram[0]]
check("search_filter keeps the addresses equal to a value", facts("search_filter", op="equal", value=ram[0]).get("candidates") == len(same))
found = facts("search_results", limit=5)
check("search_results lists them lowest first, with their values",
      found.get("candidates") == len(same) and [r["address"] for r in found.get("results", [])] == same[:5]
      and all(r["value"] == ram[0] and r["previous"] == ram[0] for r in found.get("results", [])))
check("nothing has changed since, so 'not equal to before' leaves none", facts("search_filter", op="not_equal").get("candidates") == 0)
check("search_undo takes that filter back", facts("search_undo").get("candidates") == len(same))
words = [ram[i] | ram[i + 1] << 8 for i in range(0, len(ram), 2)]
check("a search over 16-bit values has half as many candidates", facts("search_start", domain="RAM", size=2).get("candidates") == len(words))
big = [i * 2 for i, w in enumerate(words) if w > 1000]
check("search_filter compares 16-bit values", facts("search_filter", op="greater", value=1000).get("candidates") == len(big))
check("and lists their addresses", [r["address"] for r in facts("search_results", limit=500).get("results", [])] == big[:500])
error, text, _ = call("search_start", domain="Bus")
check("search_start says a bus cannot be searched", error and "bus" in text)
error, text, _ = call("search_filter", op="about")
check("search_filter names the comparisons it knows", error and "not_equal" in text)

# watch: values named once and told with every answer that lands on a frame
steps_now = struct.unpack("<I", ram[4:8])[0]
watched = facts("watch", watches=[{"name": "steps", "property": "Steps"},
                                  {"domain": "RAM", "address": 4, "type": "u32"},
                                  {"domain": "NO SUCH", "address": 0}]).get("watches", [])
check("watch answers with the values now: a property as it reads, memory as a number",
      watched[:2] == [{"name": "steps", "value": str(steps_now)}, {"name": "RAM 0x4 u32", "value": steps_now}])
check("a watch that cannot be read says why and does not spoil the others",
      len(watched) == 3 and watched[2]["name"] == "NO SUCH 0x0 u8" and "NO SUCH" in watched[2].get("error", ""))
check("status carries the watches", facts("status").get("watches") == watched)
error, text, _ = call("watch", watches=[{"address": 4}])
check("a watch with nothing to read is refused", error and "Watch 1" in text)
check("the refused list replaced nothing", facts("status").get("watches") == watched)
check("an empty list ends watching", facts("watch", watches=[]).get("watches") == [] and "watches" not in facts("status"))

if not changes:
    # nobody allowed changes: each tool that changes something says how they are allowed
    for tool, arguments in (("add_marker", {"frame": 1, "text": "x"}), ("set_buttons", {"frame": 0, "buttons": {"P1 A": True}}),
                            ("seek", {"frame": 0}), ("write_memory", {"domain": "RAM", "address": 0, "values": [1]}),
                            ("advance_until", {"property": "Steps", "op": "changed"})):
        error, text, _ = call(tool, **arguments)
        check("%s is refused while changes are not allowed" % tool, error and "Allow the agent to make changes" in text)
    check("refused changes changed nothing", facts("get_input", count=1000)["entries"] == movie
          and bytes.fromhex(facts("read_memory", domain="RAM", address=0, count=16)["bytes_hex"]) == ram[:16])


def all_input():
    return facts("get_input", count=1000)["entries"]


def ram_now():
    return bytes.fromhex(facts("read_memory", domain="RAM", address=0, count=4096)["bytes_hex"])


if mode == "window-changes":
    last = len(movie)
    # input: pressed by name over a range, seen, and taken back by one undo
    answer = facts("set_buttons", frame=5, count=3, buttons={"P1 A": True})
    check("set_buttons answers with the movie's length", answer.get("movie_length") == last)
    edited = all_input()
    check("set_buttons changed frames 5 to 7 and no others",
          [i for i in range(last) if edited[i] != movie[i]] == [5, 6, 7] and all("A" in edited[i] for i in (5, 6, 7)))
    # before that first change, the work as it was had been kept as a branch
    branches = facts("list_branches").get("branches", [])
    check("a branch was saved before the first change", len(branches) == 1 and branches[0]["note"].startswith("Before the agent's changes"))
    check("status says the project has unsaved changes", facts("status").get("unsaved_changes") is True)
    facts("undo")
    check("undo takes the whole edit back", all_input() == movie)
    error, text, _ = call("set_buttons", frame=0, buttons={"No Such Button": True})
    check("set_buttons names a button that does not exist", error and "No Such Button" in text)
    error, text, _ = call("set_buttons", frame=0)
    check("set_buttons with nothing named is refused", error and "at least one" in text)
    error, text, _ = call("insert_frames", frame=last + 9)
    check("insert_frames past the movie's end is refused", error and str(last) in text)
    check("the refused edits changed nothing", all_input() == movie)
    # input lines written as given, also past the end, and undone in one step
    facts("set_input", frame=last - 1, entries=[movie[1], movie[1], movie[1]])
    check("set_input writes lines and lengthens the movie", all_input() == movie[:-1] + [movie[1]] * 3)
    error, text, _ = call("set_input", frame=0, entries=["|this is not a line|"])
    check("set_input refuses a line that is not one of the movie's", error and "Line 1" in text)
    facts("undo")
    check("undo takes the written lines back", all_input() == movie)
    # frames inserted and deleted
    check("insert_frames lengthens the movie", facts("insert_frames", frame=0, count=2).get("movie_length") == last + 2)
    check("the input moved later", all_input()[2:] == movie)
    check("delete_frames shortens it again", facts("delete_frames", frame=0, count=2).get("movie_length") == last)
    check("the input is the movie's again", all_input() == movie)
    # a marker, and branches
    facts("add_marker", frame=9, text="placed by the witness")
    placed = [m for m in facts("list_markers").get("markers", []) if m.get("placed_by_user")]
    check("add_marker places a marker a person can tell from the movie's own",
          placed == [{"frame": 9, "text": "placed by the witness", "placed_by_user": True}])
    check("save_branch saves a second branch", facts("save_branch", note="made by the witness").get("number") == 2)
    notes = [b["note"] for b in facts("list_branches").get("branches", [])]
    check("the new branch has its note", len(notes) == 2 and notes[1] == "made by the witness")
    error, text, _ = call("load_branch", number=7)
    check("load_branch refuses a branch that does not exist", error and "between 1 and 2" in text)
    # watched values come with a seek, and advance_until plays until a value meets a condition
    steps = {"domain": "RAM", "address": 4, "type": "u32"}
    facts("watch", watches=[dict(steps, name="steps")])
    start = facts("seek", frame=0)
    at_start = facts("read_memory", count=1, **steps)["values"][0]
    check("a seek's answer carries the watched value", start.get("watches") == [{"name": "steps", "value": at_start}])
    until = facts("advance_until", op="greater", value=3, **steps)
    check("advance_until stops on the frame where the value first meets the condition",
          until.get("met") is True and until.get("stopped_at") == "condition" and until.get("frame", 0) > 0
          and until.get("frames_played") == until.get("frame") and until.get("watches", [{}])[0].get("value", 0) > 3)
    check("and status agrees on the frame", facts("status").get("frame") == until.get("frame"))
    check("one frame earlier the value did not meet it yet",
          facts("seek", frame=until.get("frame", 1) - 1).get("watches", [{}])[0].get("value", 99) <= 3)
    facts("seek", frame=0)
    changed = facts("advance_until", property="Steps", op="changed")
    check("advance_until with 'changed' stops when a property first differs",
          changed.get("met") is True and changed.get("watches", [{}])[0].get("value") != at_start)
    facts("seek", frame=0)
    limit = facts("advance_until", op="equal", value=999999, max_frames=5, **steps)
    check("advance_until stops after max_frames when the condition is never met",
          limit.get("met") is False and limit.get("stopped_at") == "max_frames" and limit.get("frame") == 5 and limit.get("frames_played") == 5)
    facts("seek", frame=last - 2)
    ended = facts("advance_until", op="equal", value=999999, **steps)
    check("advance_until stops at the movie's end", ended.get("met") is False and ended.get("stopped_at") == "movie_end" and ended.get("frame") == last)
    error, text, _ = call("advance_until", op="equal", value=999999, **steps)
    check("at the movie's end advance_until says there is nothing left to play", error and "movie's end" in text)
    error, text, _ = call("advance_until", domain="NO SUCH", address=0, op="equal", value=1)
    check("advance_until names a value it cannot read", error and "NO SUCH" in text)
    error, text, _ = call("advance_until", property="Test.Name", op="greater", value=1)
    check("advance_until takes a value that is not a number only for 'changed'", error and "'changed'" in text)
    facts("watch", watches=[])
    check("with watching ended, a seek's answer carries no watches", "watches" not in facts("seek", frame=3))

    # the playback position: back, then to the end again - and after all of the above the
    # machine at the movie's last frame holds the reference memory
    check("seek goes back to a frame", facts("seek", frame=10).get("frame") == 10 and facts("status").get("frame") == 10)
    check("memory in the middle of the movie is not the end's", ram_now() != ram)
    check("seek goes forward to the movie's end", facts("seek", frame=last).get("frame") == last)
    check("after every edit was undone, the movie plays to the reference memory", ram_now() == ram)
    error, text, _ = call("seek", frame=last + 50)
    check("seek refuses a frame past the movie's end", error and str(last) in text)
    check("load_branch loads the first branch", facts("load_branch", number=1).get("number") == 1)
    check("the input after the load is the movie's", all_input() == movie)

if changes:
    # writing to the machine: read back, and said by status from then on
    check("status says memory has not been written yet", facts("status").get("memory_written_by_agent") is False)
    frame_now = facts("status").get("frame")
    check("write_memory writes bytes", facts("write_memory", domain="RAM", address=0x800, values=[171, 205]).get("bytes_written") == 2)
    check("the bytes written are read back", facts("read_memory", domain="RAM", address=0x800, count=2).get("bytes_hex") == "ABCD")
    facts("write_memory", domain="RAM", address=0x810, values=[0x1234, -2], type="s16", big_endian=True)
    check("write_memory writes wider values in the byte order asked for",
          facts("read_memory", domain="RAM", address=0x810, count=4).get("bytes_hex") == "1234FFFE")
    after = facts("status")
    check("status says memory was written, and on which frame",
          after.get("memory_written_by_agent") is True and after.get("first_memory_write_frame") == frame_now)
    # the search an agent really runs: remember, change something, keep what changed
    facts("search_start", domain="RAM")
    old = facts("read_memory", domain="RAM", address=0x900, count=1)["values"][0]
    facts("write_memory", domain="RAM", address=0x900, values=[(old + 5) & 255])
    check("after one byte was written, 'not equal to before' leaves that byte alone", facts("search_filter", op="not_equal").get("candidates") == 1)
    check("and after the filter its value now is the one remembered",
          facts("search_results").get("results") == [{"address": 0x900, "value": (old + 5) & 255, "previous": (old + 5) & 255}])
    check("so 'equal to before' keeps it", facts("search_filter", op="equal").get("candidates") == 1)
    error, text, _ = call("write_memory", domain="RAM", address=0, values=[300])
    check("write_memory refuses a value that does not fit", error and "fits in u8" in text)
    error, text, _ = call("write_memory", domain="VRAM", address=0, values=[1])
    check("write_memory refuses memory that cannot be written", error and "cannot be written" in text)
    error, text, _ = call("write_memory", domain="RAM", address=4095, values=[1, 2])
    check("write_memory refuses a write past the end", error and "past its end" in text)
    check("the refused writes wrote nothing", facts("read_memory", domain="RAM", address=4095, count=1).get("values") == [ram[4095]])
    # a game's named value
    check("set_property sets a value from text", facts("set_property", name="Steps", value="7").get("value") == "7")
    check("the value set is read back", facts("get_property", name="Steps").get("value") == "7")
    error, text, _ = call("set_property", name="No Such Value", value="1")
    check("set_property names a value that does not exist", error and "No Such Value" in text)
    error, text, _ = call("set_register", name="PC", value=0)
    check("set_register names a register that does not exist", error and "PC" in text)
    if not window:
        error, text, _ = call("add_marker", frame=1, text="x")
        check("a change the window makes is handed to the frontend", error and "window" in text)
        error, text, _ = call("advance_until", property="Steps", op="changed")
        check("advance_until needs the window too", error and "window" in text)

agent.stdin.close()
agent.wait()
for line in wrong:
    print("WRONG: " + line)
sys.exit(1 if wrong else 0)
