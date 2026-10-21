-- Level B input polls of the synthetic witness (issue 344): event.oninputpoll on
-- a core that reports its reads of the controls.
--
-- The synth calls the host at each INPUT its machine executes, part way through
-- the frame (package-box/synth_wbx.c, SetInputPollCallback), and gridWalker reads
-- its pad once in each frame it plays. So a registered function is called once
-- a frame, inside it, where it can read the machine's memory; and removed, it
-- is called no more. A second one is left registered when Chimera closes.
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

local calls, cursorX = 0, nil
local ok, id = pcall(event.oninputpoll, function()
	calls = calls + 1
	cursorX = memory.readbyte(1, "RAM")
end, "count the reads")
check(ok, "event.oninputpoll was refused: " .. tostring(id))
check(type(id) == "string" and #id > 0, "event.oninputpoll gave no id")

-- The project's movie is five frames and a headless run pauses where it ends,
-- so everything happens inside it.
emu.frameadvance()
local before = calls
emu.frameadvance()
check(calls == before + 1, "a frame the game read its pad in made " .. (calls - before) .. " calls, not one")
check(cursorX ~= nil and cursorX == memory.readbyte(1, "RAM"), "the function did not read the machine's memory: " .. tostring(cursorX))

event.unregisterbyid(id)
before = calls
emu.frameadvance()
check(calls == before, "the function was still called after it was removed (" .. (calls - before) .. " calls)")

-- one more, left registered when the script ends, as most scripts leave
-- theirs: Chimera closes the core before the script, and taking the function
-- off a closed core must not stop Chimera with an error
event.oninputpoll(function() end, "left registered")
finish("OK", "")
