-- Level B-properties of the synthetic witness: a core's game properties, read
-- and written by name (docs/game-cores.md).
--
-- The synth exports a property table (package-box/synth_wbx.c) naming places in
-- its RAM. This script checks the frontend hands it over whole - the names in
-- the core's order, the types it gives - that a property reads the same bytes
-- memory.* reads, and that a property set by name is what the game plays on:
-- the cursor moved by game.set is drawn where it was put.
--
-- Job description is read from the file named by the CHIMERA_JOB env var:
--   meta=<path for result metadata>

local function writeAll(path, data)
	local f = assert(io.open(path, "wb"))
	f:write(data)
	f:close()
end

-- the first result is the one kept: client.exit() only asks, and the script
-- runs on until the frontend stops it
local metaPath
local finished = false
local function finish(status, detail)
	if finished then return end
	finished = true
	if metaPath then
		writeAll(metaPath, "status=" .. status .. "\ndetail=" .. (detail or "") .. "\n")
	end
	client.exit()
end

local jobPath = os.getenv("CHIMERA_JOB")
if jobPath == nil then error("CHIMERA_JOB env var not set") end
for line in io.lines(jobPath) do
	local k, v = line:match("^([^=]+)=(.*)$")
	if k == "meta" then metaPath = v end
end

local function check(ok, detail)
	if not ok then finish("FAIL", detail) end
end

-- the names, in the core's order
local names = game.list()
local want = { "Status", "Cursor.X", "Cursor.Y", "Steps", "Started" }
check(#names == #want, "game.list() has " .. #names .. " names, the core exports " .. #want)
for i, name in ipairs(want) do
	check(names[i] == name, "game.list()[" .. i .. "] is " .. tostring(names[i]) .. ", not " .. name)
end

-- The project's movie is five frames, the last four holding Down, and a
-- headless run pauses where it ends - so everything here happens inside it,
-- and nothing assumes where the cursor stands, only that Down never moves it
-- sideways.
emu.frameadvance()
check(game.get("Started") == true, "Started reads " .. tostring(game.get("Started")) .. " once the game has run, not true")
check(game.get("Cursor.X") == memory.readbyte(1, "RAM") and game.get("Cursor.Y") == memory.readbyte(2, "RAM"),
	"the cursor reads (" .. tostring(game.get("Cursor.X")) .. "," .. tostring(game.get("Cursor.Y")) .. "), RAM holds ("
	.. memory.readbyte(1, "RAM") .. "," .. memory.readbyte(2, "RAM") .. ")")
check(game.describe("Status").label == "Playing", "Status is described as " .. tostring(game.describe("Status").label) .. ", not Playing")
check(game.describe("Steps").type == "u32" and game.describe("Steps").size == 4, "Steps is not described as a four-byte u32")

-- a u32 is four bytes, little-endian
game.set("Steps", 0x01020304)
check(memory.readbyte(4, "RAM") == 4 and memory.readbyte(7, "RAM") == 1, "game.set wrote Steps in the wrong byte order")
check(game.get("Steps") == 0x01020304, "Steps reads back " .. tostring(game.get("Steps")))
game.set("Steps", 0)

-- the game plays on what was set: the cursor, put two cells right, is drawn there
local function cellPixel(cx, cy) return memory.readbyte((cy * 8 + 4) * 128 + cx * 8 + 4, "VRAM") end
local x, y = game.get("Cursor.X"), game.get("Cursor.Y")
local cursorInk = cellPixel(x, y)
check(cursorInk ~= cellPixel(x + 2, y), "the cursor's cell and the one it will be moved to look the same ("
	.. tostring(cursorInk) .. "); this test cannot tell them apart")
game.set("Cursor.X", x + 2)
emu.frameadvance()
local nowY = game.get("Cursor.Y")
check(game.get("Cursor.X") == x + 2, "Cursor.X reads " .. tostring(game.get("Cursor.X")) .. " a frame after it was set to " .. (x + 2))
check(cellPixel(x + 2, nowY) == cursorInk, "the game did not draw the cursor where game.set put it")
check(cellPixel(x, nowY) ~= cursorInk and cellPixel(x, y) ~= cursorInk, "the cursor is still drawn in its old column")

-- a name the core does not have reads as nothing and sets nothing
check(game.get("No Such Property") == nil, "game.get of an unknown name returned something")
check(game.set("No Such Property", 1) == false, "game.set of an unknown name said it set it")
check(game.set("Cursor.Y", 2) == true and game.get("Cursor.Y") == 2, "game.set of a known name did not say it set it")

finish("OK", "")
