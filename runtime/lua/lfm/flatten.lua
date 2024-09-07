local M = { _NAME = ... }

local lfm = lfm

local fm = lfm.fm

---Set the flatten level of the current directory.
---```lua
---    -- flatten one level deep
---    M.flatten(1)
---
---    -- reset flatten level
---    M.flatten(0)
--
---    -- increment flatten level
---    M.flatten('+')
--
---    -- decrement flatten level
---    M.flatten('-')
---```
---@param level integer | ("+"|"-") The level or "+"/"-" to increment/decrement respectively
function M.flatten(level)
	if level == "+" then
		level = fm.flatten_level() + 1
	elseif level == "-" then
		level = fm.flatten_level() - 1
	end
	fm.flatten(level --[[@as integer]])
end

---Increment the flatten level of the current directory. Sets "nodirfirst".
---```lua
---    M.increment()
---```
function M.increment()
	if fm.flatten_level() == 0 then
		fm.sort({ dirfirst = false })
	end
	M.flatten("+")
end

---Decrement the flatten level of the current directory. Sets "dirfirst" after
---reaching a flatten level of 0.
---```lua
---    M.deccrement()
---```
function M.decrement()
	M.flatten("-")
	if fm.flatten_level() == 0 then
		fm.sort({ dirfirst = true })
	end
end

return M
