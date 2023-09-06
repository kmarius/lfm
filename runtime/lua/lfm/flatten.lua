local lfm = lfm

local fm = lfm.fm

local M = {}

---Set the flatten level of the current directory.
---@param level number | string the level or "+" or "-"
function M.flatten(level)
	if level == "+" then
		level = fm.flatten_level() + 1
	elseif level == "-" then
		level = fm.flatten_level() - 1
	end
	fm.flatten(level)
end

---Increment the flatten level of the current directory. Sets "nodirfirst".
function M.flatten_inc()
	if fm.flatten_level() == 0 then
		fm.sortby("nodirfirst")
	end
	M.flatten("+")
end

---Decrement the flatten level of the current directory. Sets "dirfirst" after
---reaching a flatten level of 0.
function M.flatten_dec()
	M.flatten("-")
	if fm.flatten_level() == 0 then
		fm.sortby("dirfirst")
	end
end

return M
