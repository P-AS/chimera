-- client.getconfig and client.gettool in the window itself, where every view
-- model is: each of Chimera's setting names reads one of the window's
-- settings (a name tied to a setting the window does not have reads nil),
-- and each tool getavailabletools lists opens and gives its object. The
-- window quits with 0 when all is well and 1 when not, saying what on
-- standard error. Run by meson as "lua config names".
emu.frameadvance()
local bad, names, tools = {}, 0, {}
-- nil when it is: no data folder move waiting
local mayBeNil = {DataDirectoryPending = true}
-- a name tied to a property the view model does not have is an error
local ok, why = pcall(function()
	for name, value in pairs(client.getconfig()) do
		names = names + 1
		if value == nil and not mayBeNil[name] then bad[#bad + 1] = name end
		if type(value) == "table" then
			for inner, v in pairs(value) do
				if v == nil then bad[#bad + 1] = name .. "." .. inner end
			end
		end
	end
	tools = client.getavailabletools()
	for i = 0, #tools do
		if client.gettool(tools[i]) == nil then bad[#bad + 1] = "gettool " .. tools[i] end
	end
end)
if not ok then bad[#bad + 1] = tostring(why) end
table.sort(bad)
io.stderr:write(string.format("lua config names: %d names, %d tools, %d bad%s\n", names, tools[0] and #tools + 1 or 0, #bad,
	#bad > 0 and (": " .. table.concat(bad, ", ")) or ""))
-- the frame played is a change to the project: quitting does not ask about it
chimera.customize.neverAskSave = true
chimera.toolWindows.exit((#bad == 0 and names > 0) and 0 or 1)
