local M =  {}

function M.flatten(level)
	if level == "+" then
		level = fm.flatten_level() + 1
	elseif level == "-" then
		level = fm.flatten_level() - 1
	end
	fm.flatten(level)
end

function M.flatten_inc()
	if fm.flatten_level() == 0 then
		fm.sortby("nodirfirst")
	end
	flatten("+")
end

function M.flatten_dec()
	flatten("-")
	if fm.flatten_level() == 0 then
		fm.sortby("dirfirst")
	end
end

return M
