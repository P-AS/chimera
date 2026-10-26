-- client.getconfig written out as JSON, with the list it is read by, to the
-- file CHIMERA_TEST_DUMP names; then the window quits. For chimera-import.py.
local function encode(v)
	local t = type(v)
	if v == nil then return "null" end
	if t == "boolean" then return tostring(v) end
	if t == "number" then return math.type(v) == "integer" and tostring(v) or string.format("%.17g", v) end
	if t == "string" then
		return '"' .. string.gsub(v, '[%c"\\]', function(c) return string.format("\\u%04x", string.byte(c)) end) .. '"'
	end
	local parts = {}
	for k, x in pairs(v) do parts[#parts + 1] = encode(tostring(k)) .. ": " .. encode(x) end
	return "{" .. table.concat(parts, ", ") .. "}"
end
-- with a game named, the dump waits for it (a script starts again when one opens)
if os.getenv("CHIMERA_TEST_WAIT_OPEN") and not chimera.session.isOpen then
	while true do emu.yield() end
end
-- and the window's own settings named in CHIMERA_TEST_KEYS ("vm.property,...")
local keys = {}
for key in string.gmatch(os.getenv("CHIMERA_TEST_KEYS") or "", "[^,]+") do
	local vm, name = string.match(key, "^(%w+)%.(%w+)$")
	keys[key] = chimera[vm][name]
end
local out = assert(io.open(os.getenv("CHIMERA_TEST_DUMP"), "w"))
out:write('{"names": ', encode(__chimera_config_names()), ', "config": ', encode(client.getconfig()), ', "keys": ', encode(keys), "}\n")
out:close()
chimera.toolWindows.exit(0)
