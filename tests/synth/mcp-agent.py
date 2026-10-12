#!/usr/bin/env python3
"""Plays the part of an AI agent's program for the MCP server's test
(docs/mcp-server.md): starts chimera-mcp, speaks MCP to it on its standard
input and output, and checks what the tools say about the synthetic core
against files the other tests already trust.

Usage: mcp-agent.py <chimera-mcp> <reference ram> <reference vram> <movie.txt> <last frame .tga>

A running Chimera must be serving (tests/synth/run-witness.sh starts
chimera-run --mcp-serve for that). Prints one line for each thing that is
wrong and exits 1, or prints nothing and exits 0.
"""
import base64
import json
import struct
import subprocess
import sys
import zlib

mcp, ram_path, vram_path, movie_path, tga_path = sys.argv[1:6]
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
check("status says changes are not allowed", status.get("changes_allowed") is False)
check("status says the core has not stopped", status.get("core_stopped") is False)
check("status gives the picture's size", status.get("picture", {}).get("width", 0) > 0)

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
    check("screenshot is the last frame's picture", (width, height, pixels) == tga_pixels(tga_path))
    half = facts("screenshot", max_width=width // 2)
    check("screenshot scales a picture down to max_width", half.get("width") == width // 2 and half.get("picture_width") == width)

# a tool that needs the window says so here, where there is none
error, text, _ = call("list_markers")
check("a tool the frontend runs is handed to the frontend", error and "window" in text)

agent.stdin.close()
agent.wait()
for line in wrong:
    print("WRONG: " + line)
sys.exit(1 if wrong else 0)
