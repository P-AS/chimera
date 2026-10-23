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

-- memorysavestate: the core's own state in memory, by an id ----------------
memorysavestate = {}
function memorysavestate.savecorestate() return C.session.saveCoreState() end
function memorysavestate.loadcorestate(id) C.session.loadCoreState(tostring(id)) end
function memorysavestate.removestate(id) C.session.removeState(tostring(id)) end
function memorysavestate.clearstatesfrommemory() C.session.clearStates() end

-- userdata: values a script keeps for this session, as Chimera's (in memory,
-- shared by every script; a boolean, a number or a string each)
userdata = {}
local bag = {}
function userdata.set(name, value)
	local t = type(value)
	if t ~= "nil" and t ~= "boolean" and t ~= "number" and t ~= "string" then error("Invalid type for userdata", 2) end
	bag[tostring(name)] = value
end
function userdata.get(key) return bag[tostring(key)] end
function userdata.clear() bag = {} end
function userdata.remove(key)
	key = tostring(key)
	if bag[key] == nil then return false end
	bag[key] = nil
	return true
end
function userdata.containskey(key) return bag[tostring(key)] ~= nil end
function userdata.get_keys()
	local keys = {}
	for k in pairs(bag) do keys[#keys + 1] = k end
	table.sort(keys)
	return keys
end

-- game: a game core's properties by name (docs/game-cores.md) -------------
game = {}
local function describeOf(name)
	local text = C.game.describe(tostring(name))
	if text == "" then return nil end
	local d = {}
	for k, v in string.gmatch(text, "([^\t\n]+)\t([^\n]*)") do d[k] = v end
	return d
end
local function bytesTable(s)
	local t = {}
	for i = 1, #s do t[i] = string.byte(s, i) end
	return t
end
local function getOne(name)
	local kind = C.game.kind(name)
	local v = C.game.get(name)
	if C.game.lastError ~= "" then console.log("game.get: " .. C.game.lastError) return nil end
	if kind == "bytes" then return bytesTable(v) end
	return v
end
-- an array by its own name: its elements, from 1 as Lua counts
local function arrayOf(name)
	if string.find(name, "%]%s*$") then return nil end
	local d = describeOf(name)
	if d == nil or tonumber(d.count) == nil or tonumber(d.count) <= 1 then return nil end
	return tonumber(d.first), tonumber(d.count)
end
function game.list()
	local names = {}
	for n in string.gmatch(C.game.list(), "[^\n]+") do names[#names + 1] = n end
	return names
end
function game.get(name)
	name = tostring(name)
	local first, count = arrayOf(name)
	if first ~= nil then
		local t = {}
		for i = 0, count - 1 do t[i + 1] = getOne(name .. "[" .. (first + i) .. "]") end
		return t
	end
	return getOne(name)
end
local function setOne(name, value)
	local ok
	if type(value) == "table" then
		local parts = {}
		for i = 1, #value do parts[i] = string.char(value[i] & 0xFF) end
		ok = C.game.set(name, table.concat(parts), true)
	else
		ok = C.game.set(name, value, false)
	end
	if not ok then console.log("game.set: " .. C.game.lastError) end
	return ok
end
function game.set(name, value)
	name = tostring(name)
	local first, count = arrayOf(name)
	if first ~= nil and C.game.kind(name) ~= "bytes" then
		if type(value) ~= "table" then
			console.log("game.set: \"" .. name .. "\" is an array of " .. count .. ", so it takes a table of them")
			return false
		end
		local all = true
		for i = 0, count - 1 do
			if value[i + 1] ~= nil then all = setOne(name .. "[" .. (first + i) .. "]", value[i + 1]) and all end
		end
		return all
	end
	return setOne(name, value)
end
function game.describe(name)
	local d = describeOf(name)
	if d == nil then return nil end
	for _, k in ipairs({"offset", "size", "count", "first", "stride", "bit", "bits"}) do d[k] = tonumber(d[k]) end
	d.writable = d.writable == "true"
	return d
end

-- input: what the person's own devices hold, as Chimera's -----------------
input = {}
function input.get()
	local held = {}
	for name in string.gmatch(C.input.held(), "[^\n]+") do held[name] = true end
	return held
end
function input.getmouse()
	local b = C.input.mouseButtons
	return {X = C.input.mouseX, Y = C.input.mouseY, Left = b & 1 ~= 0, Middle = b & 2 ~= 0, Right = b & 4 ~= 0,
		XButton1 = b & 8 ~= 0, XButton2 = b & 16 ~= 0, Wheel = C.input.mouseWheel}
end
-- the pointer's place; the gamepads' sticks are not followed here
function input.get_pressed_axes() return {["WMouse X"] = C.input.mouseX, ["WMouse Y"] = C.input.mouseY} end

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
-- Chimera's Encode Video without the window: nil when it began, else why
-- not; the command's container is the file's (the path as given stands)
function client.encodevideo(path, from, to, command)
	local e = C.encoder
	if command ~= nil then e.command = tostring(command) end
	e.output = tostring(path)
	e.fromFrame = from
	e.toFrame = to
	local ok, err = pcall(e.start)
	if ok then return nil end
	return tostring(err)
end
function client.encodingvideo() return C.encoder.phase == "encoding" end

-- gameinfo -----------------------------------------------------------------
gameinfo = {}
function gameinfo.getromname() return C.session.gameName end
function gameinfo.getromhash() return "" end
function gameinfo.indatabase() return false end
function gameinfo.getboardtype() return "" end
function gameinfo.getoptions() return {} end

-- gui: drawn over the picture, in the machine's pixels; a frame's drawings
-- go when the next frame begins, so a script draws each frame, as Chimera's
gui = {}
local shapes = {}
local foreground, background, textBackground = "#FFFFFFFF", "", ""
local function colour(c, fallback)
	if c == nil then return fallback end
	if type(c) == "number" then return string.format("#%08X", math.tointeger(c) & 0xFFFFFFFF) end
	return tostring(c)
end
local function jsonOf(v)
	if type(v) == "table" then
		if #v > 0 or next(v) == nil then
			local out = {}
			for i = 1, #v do out[i] = jsonOf(v[i]) end
			return "[" .. table.concat(out, ",") .. "]"
		end
		local out = {}
		for k, x in pairs(v) do out[#out + 1] = string.format("%q", tostring(k)) .. ":" .. jsonOf(x) end
		return "{" .. table.concat(out, ",") .. "}"
	elseif type(v) == "number" then
		return math.type(v) == "integer" and tostring(v) or string.format("%.17g", v)
	elseif type(v) == "boolean" then return tostring(v)
	end
	return '"' .. string.gsub(tostring(v), '[%c"\\]', function(c) return string.format("\\u%04x", string.byte(c)) end) .. '"'
end
local function add(t) shapes[#shapes + 1] = t end
function gui.drawRectangle(x, y, w, h, line, fill) add({k = "rect", x = x, y = y, w = w, h = h, line = colour(line, foreground), fill = colour(fill, background)}) end
function gui.drawBox(x, y, x2, y2, line, fill)
	gui.drawRectangle(math.min(x, x2), math.min(y, y2), math.abs(x2 - x), math.abs(y2 - y), line, fill)
end
function gui.drawEllipse(x, y, w, h, line, fill) add({k = "ellipse", x = x, y = y, w = w, h = h, line = colour(line, foreground), fill = colour(fill, background)}) end
function gui.drawLine(x1, y1, x2, y2, c) add({k = "line", x = x1, y = y1, x2 = x2, y2 = y2, line = colour(c, foreground)}) end
function gui.drawAxis(x, y, size, c) gui.drawLine(x - size, y, x + size, y, c) gui.drawLine(x, y - size, x, y + size, c) end
local function pointsOf(t, dx, dy)
	local out = {}
	for _, pt in ipairs(t) do out[#out + 1] = {pt[1] + (dx or 0), pt[2] + (dy or 0)} end
	return out
end
function gui.drawPolygon(points, x, y, line, fill)
	add({k = "path", points = pointsOf(points, x, y), closed = true, line = colour(line, foreground), fill = colour(fill, background)})
end
-- four points: the start, two control points, the end
function gui.drawBezier(points, c)
	local p = pointsOf(points)
	if #p == 4 then add({k = "bezier", points = p, line = colour(c, foreground)}) end
end
-- angles in degrees, clockwise from the right, as Chimera's
function gui.drawPie(x, y, w, h, start, sweep, line, fill)
	add({k = "pie", x = x, y = y, w = w, h = h, start = start, sweep = sweep, line = colour(line, foreground), fill = colour(fill, background)})
end
-- a file's path, a relative one from the script's folder, as Chimera runs a
-- script in its own folder
local function fileOf(path)
	path = tostring(path)
	if path:match("^[/\\]") or path:match("^%a:[/\\]") then return path end
	local folder = (C.luaConsole and C.luaConsole.path or ""):match("^(.*)[/\\][^/\\]*$")
	return folder and (folder .. "/" .. path) or path
end
local imageEpoch = 0
function gui.drawImage(path, x, y, w, h)
	add({k = "image", file = fileOf(path), x = x, y = y, w = w or -1, h = h or -1, epoch = imageEpoch})
end
function gui.drawIcon(path, x, y, w, h) gui.drawImage(path, x, y, w, h) end
function gui.drawImageRegion(path, sx, sy, sw, sh, x, y, w, h)
	add({k = "image", file = fileOf(path), sx = sx, sy = sy, sw = sw, sh = sh, x = x, y = y, w = w or sw, h = h or sh, epoch = imageEpoch})
end
function gui.drawPixel(x, y, c) add({k = "rect", x = x, y = y, w = 1, h = 1, line = "", fill = colour(c, foreground)}) end
function gui.drawString(x, y, text, fore, back, size)
	add({k = "text", x = x, y = y, text = tostring(text), color = colour(fore, foreground), back = colour(back, textBackground), size = size or 12})
end
gui.drawText = gui.drawString
function gui.pixelText(x, y, text, fore, back)
	add({k = "text", x = x, y = y, text = tostring(text), color = colour(fore, "#FFFFFFFF"), back = colour(back, "#FF000000"), size = 8})
end
-- in the window's own pixels, from a corner
function gui.text(x, y, text, fore, anchor)
	add({k = "screentext", x = x, y = y, text = tostring(text), color = colour(fore, foreground), anchor = anchor or "topleft"})
end
function gui.clearGraphics()
	local kept = {}
	for _, s in ipairs(shapes) do if s.k == "screentext" then kept[#kept + 1] = s end end
	shapes = kept
end
function gui.cleartext()
	local kept = {}
	for _, s in ipairs(shapes) do if s.k ~= "screentext" then kept[#kept + 1] = s end end
	shapes = kept
end
function gui.addmessage(text) C.gui.addMessage(tostring(text)) end
function gui.defaultForeground(c) foreground = colour(c, "#FFFFFFFF") end
function gui.defaultBackground(c) background = colour(c, "") end
function gui.defaultTextBackground(c) textBackground = colour(c, "") end
function gui.defaultPixelFont() end
function gui.use_surface() end
function gui.DrawNew() end
function gui.DrawFinish() end
-- the images are read again the next time they are drawn
function gui.clearImageCache() imageEpoch = imageEpoch + 1 end
function __chimera_frame_begin() shapes = {} end
function __chimera_frame_end() if C.gui then C.gui.shapes = jsonOf(shapes) end end

-- event --------------------------------------------------------------------
event = {}
local named = 0
-- after each frame, as the window learns of it
function event.onframeend(fn, name) chimera.onFrame(fn) named = named + 1 return name or ("event" .. named) end
event.onframestart = event.onframeend
)LUA";
}

} // namespace chimera::vm
