-- A script's questions about TAStudio's cells (tastudio.onqueryitembg and
-- onqueryitemtext) reach the rolls the window draws. TAStudio's window makes
-- its rolls after the program has started, and a roll made then was never
-- given the script's questions: they went unasked, and a script's colours
-- and texts never showed. The window quits with 0 when the roll asked, 1
-- when not, saying how many times on standard error. Run by meson as
-- "tastudio queries".
local asked = 0
tastudio.onqueryitembg(function(row, column) asked = asked + 1 end)
tastudio.onqueryitemtext(function(row, column) asked = asked + 1 end)
client.opentasstudio()
for _ = 1, 30 do emu.frameadvance() end
io.stderr:write(string.format("tastudio queries: the roll asked %d times\n", asked))
-- the frames played are a change to the project: quitting does not ask about it
chimera.customize.neverAskSave = true
chimera.toolWindows.exit(asked > 0 and 0 or 1)
