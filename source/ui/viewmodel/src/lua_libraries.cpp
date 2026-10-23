#include "chimera/vm/lua_bridge.hpp"

namespace chimera::vm {

/* Chimera's Lua libraries, as a script written for Chimera calls them, over
 * the view models (chimera.<name>): the names, arguments and answers are
 * Chimera's (its CommonLibs and the window's libraries), the work is the
 * view models'. Tables Chimera numbers from 0 are numbered from 0 here.
 * What the prototype cannot answer yet is said where it is asked. */
const char *LuaBridge::luaLibraries()
{
	return R"LUA(
local C = chimera

local function lines(text)
	local out = {}
	for line in string.gmatch(text or "", "[^\n]+") do out[#out + 1] = line end
	return out
end

-- console ------------------------------------------------------------------
console = {}
local function joined(...)
	local t = {}
	for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
	return table.concat(t, " ")
end
function console.log(...) print(joined(...)) end
function console.writeline(...) print(joined(...)) end
function console.write(...) print(joined(...)) end
function console.clear() if C.luaConsole then C.luaConsole.clear() end end

-- bit (Chimera's, over Lua's own operators) ---------------------------------
bit = {}
function bit.band(a, b) return a & b end
function bit.bor(a, b) return a | b end
function bit.bxor(a, b) return a ~ b end
function bit.bnot(a) return ~a & 0xFFFFFFFF end
function bit.lshift(a, n) return (a << n) & 0xFFFFFFFF end
function bit.rshift(a, n) return (a & 0xFFFFFFFF) >> n end
function bit.arshift(a, n) local v = a & 0xFFFFFFFF if v >= 0x80000000 then v = v - 0x100000000 end return v >> n | (v < 0 and ~(0xFFFFFFFF >> n) or 0) end
function bit.rol(a, n) a = a & 0xFFFFFFFF n = n % 32 return ((a << n) | (a >> (32 - n))) & 0xFFFFFFFF end
function bit.ror(a, n) a = a & 0xFFFFFFFF n = n % 32 return ((a >> n) | (a << (32 - n))) & 0xFFFFFFFF end
function bit.check(a, pos) return (a >> pos) & 1 == 1 end
function bit.set(a, pos) return a | (1 << pos) end
function bit.clear(a, pos) return a & ~(1 << pos) end
function bit.byteswap_16(a) return ((a & 0xFF) << 8) | ((a >> 8) & 0xFF) end
function bit.byteswap_32(a) return string.unpack("<I4", string.pack(">I4", a & 0xFFFFFFFF)) end

-- emu ----------------------------------------------------------------------
emu = {}
-- the script waits here and goes on after the next frame
function emu.frameadvance() if coroutine.isyieldable() then coroutine.yield() end end
function emu.yield() if coroutine.isyieldable() then coroutine.yield() end end
function emu.framecount() return C.session.frame end
function emu.lagcount() return C.session.lagCount end
function emu.islagged() return C.session.lagFrame end
function emu.getsystemid() return C.session.systemName end
function emu.getcorename() return C.session.coreName end
function emu.getboardname() return "" end
function emu.getdisplaytype() return "" end
function emu.getregisters()
	local out = {}
	for _, line in ipairs(lines(C.debugger.registerList())) do
		local name, value = string.match(line, "^(.-)=(%-?%d+)$")
		if name then out[name] = math.tointeger(tonumber(value)) end
	end
	return out
end
function emu.getregister(name) return emu.getregisters()[name] end
function emu.setregister(name, value) C.debugger.setRegisterNamed(name, value) end
function emu.totalexecutedcycles() C.debugger.refresh() return C.debugger.cycles end
function emu.limitframerate(on) C.session.unthrottled = not on end
function emu.minimizeframeskip(on) C.session.autoMinimizeSkipping = on end
function emu.displayvsync() end
function emu.setrenderplanes() end
function emu.disassemble() error("emu.disassemble: the cores here offer no disassembly") end

-- memory and mainmemory ----------------------------------------------------
local function domainList()
	local out = {}
	for _, line in ipairs(lines(C.session.memoryDomains())) do
		local name, size, writable = string.match(line, "^(.-)\t(%d+)\t(%d)$")
		if name then out[#out + 1] = {name = name, size = math.tointeger(tonumber(size)), writable = writable == "1"} end
	end
	return out
end
local current = nil
local function mainDomain() local d = domainList() return d[1] and d[1].name or "" end
-- Chimera's default for memory: the system bus when there is one, else the main memory
local function currentDomain()
	if current then return current end
	for _, d in ipairs(domainList()) do if d.name == "System Bus" then return d.name end end
	return mainDomain()
end
local function makeMemory(domainOf)
	local m = {}
	local function rd(addr, n, domain) return C.session.readMemory(domain or domainOf(), addr, n) end
	local function wr(addr, bytes, domain) C.session.writeMemory(domain or domainOf(), addr, bytes) end
	function m.readbyte(addr, domain) return string.byte(rd(addr, 1, domain)) end
	function m.writebyte(addr, value, domain) wr(addr, string.char(value & 0xFF), domain) end
	function m.readbyterange(addr, length, domain)
		local s, t = rd(addr, length, domain), {}
		for i = 1, #s do t[i - 1] = string.byte(s, i) end
		return t
	end
	function m.read_bytes_as_array(addr, length, domain)
		local s, t = rd(addr, length, domain), {}
		for i = 1, #s do t[i] = string.byte(s, i) end
		return t
	end
	function m.read_bytes_as_dict(addr, length, domain)
		local s, t = rd(addr, length, domain), {}
		for i = 1, #s do t[addr + i - 1] = string.byte(s, i) end
		return t
	end
	function m.read_bytes_as_binary_string(addr, length, domain) return rd(addr, length, domain) end
	function m.writebyterange(t, domain)
		for addr, value in pairs(t) do wr(addr, string.char(value & 0xFF), domain) end
	end
	function m.write_bytes_as_array(addr, t, domain)
		local b = {}
		for i = 1, #t do b[i] = string.char(t[i] & 0xFF) end
		wr(addr, table.concat(b), domain)
	end
	function m.write_bytes_as_dict(t, domain) m.writebyterange(t, domain) end
	function m.write_bytes_as_binary_string(addr, s, domain) wr(addr, s, domain) end
	-- the sized reads and writes, each sign and order Chimera has
	for _, size in ipairs({1, 2, 3, 4}) do
		for _, signed in ipairs({false, true}) do
			local letter = signed and "i" or "I"
			local stem = (signed and "s" or "u") .. (size * 8)
			if size == 1 then
				m["read_" .. stem] = function(addr, domain) return (string.unpack("<" .. letter .. "1", rd(addr, 1, domain))) end
				m["write_" .. stem] = function(addr, value, domain) wr(addr, string.pack("<I1", value & 0xFF), domain) end
			else
				for _, order in ipairs({{"_le", "<"}, {"_be", ">"}}) do
					local fmt = order[2] .. letter .. size
					local mask = (1 << (size * 8)) - 1
					m["read_" .. stem .. order[1]] = function(addr, domain) return (string.unpack(fmt, rd(addr, size, domain))) end
					m["write_" .. stem .. order[1]] = function(addr, value, domain)
						wr(addr, string.pack(order[2] .. "I" .. size, value & mask), domain)
					end
				end
			end
		end
	end
	function m.readfloat(addr, bigendian, domain) return (string.unpack(bigendian and ">f" or "<f", rd(addr, 4, domain))) end
	function m.writefloat(addr, value, bigendian, domain) wr(addr, string.pack(bigendian and ">f" or "<f", value), domain) end
	function m.hash_region() error("hash_region: not here yet") end
	return m
end
memory = makeMemory(currentDomain)
function memory.getmemorydomainlist()
	local t = {}
	for i, d in ipairs(domainList()) do t[i - 1] = d.name end
	return t
end
function memory.getmemorydomainsize(name)
	local want = (name == nil or name == "") and currentDomain() or name
	for _, d in ipairs(domainList()) do if d.name == want then return d.size end end
	for _, d in ipairs(domainList()) do if d.name == currentDomain() then return d.size end end
	return 0
end
function memory.getcurrentmemorydomain() return currentDomain() end
function memory.getcurrentmemorydomainsize() return memory.getmemorydomainsize() end
function memory.usememorydomain(name)
	for _, d in ipairs(domainList()) do
		if d.name == name then current = name return true end
	end
	return false
end
mainmemory = makeMemory(mainDomain)
function mainmemory.getname() return mainDomain() end
function mainmemory.getcurrentmemorydomainsize() return memory.getmemorydomainsize(mainDomain()) end

-- joypad -------------------------------------------------------------------
joypad = {}
-- buttons by their names; with a controller's number, the names without its "P<n> "
local function byController(names, controller, value)
	local out = {}
	for _, name in ipairs(names) do
		if controller == nil then out[name] = value
		else
			local short = string.match(name, "^P" .. controller .. " (.+)$")
			if short then out[short] = value end
		end
	end
	return out
end
local function allButtons() return lines(C.session.buttonList()) end
function joypad.get(controller)
	local held, out = {}, {}
	for _, name in ipairs(lines(C.session.heldButtons())) do held[name] = true end
	for name, _ in pairs(byController(allButtons(), controller, false)) do out[name] = false end
	for name, v in pairs(byController(lines(C.session.heldButtons()), controller, true)) do out[name] = v end
	return out
end
joypad.getimmediate = joypad.get
function joypad.getwithmovie(controller) return joypad.get(controller) end
-- held for the next frame run, as Chimera's joypad.set
function joypad.set(buttons, controller)
	local names = {}
	for name, on in pairs(buttons) do
		if on == true then names[#names + 1] = controller and ("P" .. controller .. " " .. name) or name end
	end
	C.session.scriptHold(table.concat(names, "\n"))
end
function joypad.setanalog() error("joypad.setanalog: not here yet") end
joypad.setaxis = joypad.setanalog
function joypad.setfrommnemonicstr() error("joypad.setfrommnemonicstr: not here yet") end

-- movie --------------------------------------------------------------------
movie = {}
function movie.isloaded() return C.session.hasProject end
function movie.filename() return C.session.projectPath end
function movie.length() return C.pianoRoll.frameCount end
function movie.mode()
	if not C.session.hasProject then return "INACTIVE" end
	return C.session.frame < C.pianoRoll.frameCount and "PLAY" or "RECORD"
end
function movie.getrerecordcount() return C.session.rerecords end
function movie.getreadonly() return false end
function movie.getfps() return C.session.fps end
function movie.save(path) if path and path ~= "" then C.session.saveProject(path) else C.session.saveProject() end end
function movie.play_from_start() if C.session.projectPath ~= "" then C.session.openProject(C.session.projectPath) end end
function movie.stop() C.session.stopMovie() end
function movie.getinput(frame, controller)
	local pressed, values, out = {}, {}, {}
	for _, line in ipairs(lines(C.session.movieInput(frame))) do
		local name, value = string.match(line, "^(.-)=(%-?%d+)$")
		if name then values[name] = math.tointeger(tonumber(value)) else pressed[#pressed + 1] = line end
	end
	for name, _ in pairs(byController(allButtons(), controller, false)) do out[name] = false end
	for name, v in pairs(byController(pressed, controller, true)) do out[name] = v end
	for name, v in pairs(values) do
		local short = controller and string.match(name, "^P" .. controller .. " (.+)$") or name
		if short then out[short] = v end
	end
	return out
end

-- client -------------------------------------------------------------------
client = {}
function client.pause() C.session.pause() end
function client.unpause() C.session.play() end
function client.togglepause() if C.session.running then C.session.pause() else C.session.play() end end
function client.ispaused() return not C.session.running end
function client.isturbo() return false end
function client.isseeking() return false end
function client.speedmode(percent) C.session.speedPercent = percent end
function client.frameskip(n) C.session.setFrameSkip(n) end
function client.reboot_core() C.session.restart() end
function client.closerom() C.session.close() end
function client.openrom(path) C.session.openGame(path) end
function client.openproject(path) C.session.openProject(path) end
function client.getversion() return C.help and C.help.buildName or "Qt prototype" end
function client.get_approx_framerate() return C.session.fps end
function client.SetSoundOn(on) C.session.soundOn = on end
function client.GetSoundOn() return C.session.soundOn end
function client.displaymessages(on) C.session.showMessages = on end
function client.clearautohold() C.session.clearAutohold() end
function client.exit() error("client.exit: a script does not close the window here") end

-- gameinfo -----------------------------------------------------------------
gameinfo = {}
function gameinfo.getromname() return C.session.gameName end
function gameinfo.getromhash() return "" end
function gameinfo.indatabase() return false end
function gameinfo.getboardtype() return "" end
function gameinfo.getoptions() return {} end

-- event --------------------------------------------------------------------
event = {}
local named = 0
-- after each frame, as the window learns of it
function event.onframeend(fn, name) chimera.onFrame(fn) named = named + 1 return name or ("event" .. named) end
event.onframestart = event.onframeend
)LUA";
}

} // namespace chimera::vm
