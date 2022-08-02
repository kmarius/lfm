local lfs = require("lfs")
_G.lfs = nil  -- lua-filesystem creates this global for some reason...

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
	local attributes = lfs.attributes
	return coroutine.wrap(function()
		for f in lfs.dir(path) do
			if f ~= "." and f ~= ".." then
				local _f = path .. "/" .. f
				if not fn or fn(_f) then
					yield(_f)
				end
				if attributes(_f, "mode") == "directory" then
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
