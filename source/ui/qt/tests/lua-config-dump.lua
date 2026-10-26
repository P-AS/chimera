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
local out = assert(io.open(os.getenv("CHIMERA_TEST_DUMP"), "w"))
out:write('{"names": ', encode(__chimera_config_names()), ', "config": ', encode(client.getconfig()), "}\n")
out:close()
chimera.toolWindows.exit(0)
