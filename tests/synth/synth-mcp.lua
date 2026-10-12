-- The MCP server's test in the real window (docs/mcp-server.md): Chimera was
-- started with --mcp-server and a project. This script plays the project's
-- movie to its end, pauses, says it is ready, and then only keeps Chimera
-- running while the test's agent (mcp-agent.py) talks to the server. It ends
-- Chimera when the agent's side says it is done, or after two minutes.
--
-- Job description is read from the file named by the CHIMERA_JOB env var:
--   ready=<file written when the movie has been played>
--   done=<file whose appearance ends the run>

local jobPath = os.getenv("CHIMERA_JOB")
if jobPath == nil then error("CHIMERA_JOB env var not set") end
local job = {}
for line in io.lines(jobPath) do
	local k, v = line:match("^([^=]+)=(.*)$")
	if k then job[k] = v end
end

local function exists(path)
	local f = io.open(path, "rb")
	if f then f:close() end
	return f ~= nil
end

-- TAStudio is what a person has open when they work on a movie, and it is
-- where the server's tools make their changes; a run nobody clicks in has to
-- open it itself. It owns the playback position, so it is asked to play to
-- the movie's end.
if not tastudio.engaged() then
	client.opentasstudio()
end
pcall(function() client.speedmode(6400) end)
local length = movie.length()
pcall(function() tastudio.setplayback(length) end)
local spins = 0
while emu.framecount() < length and spins < 20000 do
	client.unpause()
	emu.yield()
	spins = spins + 1
end
client.pause()

local f = assert(io.open(job.ready, "wb"))
f:write("frames=" .. emu.framecount() .. "\n")
f:close()

local started = os.time()
while not exists(job.done) and os.time() - started < 120 do
	emu.yield()
end
client.exit()
