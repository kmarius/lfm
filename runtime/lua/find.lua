local stat = require("posix.sys.stat")
local dirent = require("posix.dirent")

local M = {}

---Recursive dirwalk with an optional filter.
---
---```
--- for _, file in find(".", function(f) return string.sub(f, 1, 1) ~= "." end) do
---     print(file)
--- end
---
---```
---@param path string Start path.
---@param fn function Filter function called on the path of each file.
---@return fun(...: nil):...
local function find(path, fn)
	local yield = coroutine.yield
	return coroutine.wrap(function()
		local stat_stat = stat.stat
		local s_isdir = stat.S_ISDIR
		for f in dirent.files(path) do
			if f ~= "." and f ~= ".." then
				local _f = path .. "/" .. f
				if not fn or fn(_f) then
					yield(_f)
				end
				if s_isdir(stat_stat(_f).st_mode) == 1 then
					for n in find(_f, fn) do
						yield(n)
					end
				end
			end
		end
	end)
end

M.find = find

return M
