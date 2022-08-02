local lfm = lfm

local eval = lfm.eval
local feedkeys = lfm.feedkeys

local M = {}

---Get dirname of path.
---@param path string
---@return string?
function M.dirname(path)
	if not path then return nil end
	if not string.match(path, "^/") then
		path = "./"..path
	end
	return (string.gsub(path, "/[^/]*$", ""))
end

---Get basename of path.
---@param path string
---@return string?
function M.basename(path)
	if not path then return nil end
	return (string.gsub(path, "^.*/", ""))
end

---Split a file into its base name and extension
---@param file string
---@return string? name
---@return string? extension
function M.file_split(file)
	if not file then return nil end
	local i, j = string.find(file, "%.[^.]*$")
	if not i or i == 1 then
		return file
	else
		return string.sub(file, 1, i-1), string.sub(file, i+1, j)
	end
end

---Construct a function that, when called, executes line as an expression.
---@param line string
---@return function function
function M.expr(line)
	return function()
		eval(line)
	end
end

---Create a function that calls all functions given as the argument.
---
---```
--- c(lfm.load_clear, lfm.ui.draw)()
---```
---
---@vararg function
---@return function
function M.c(...)
	local t = {...}
	if #t == 0 then
		return function() end
	elseif #t == 1 then
		return t[1]
	elseif #t == 2 then
		local f = t[1]
		local g = t[2]
		return function(...)
			f(...)
			g(...)
		end
	else
		return function(...)
			for _, f in pairs(t) do
				f(...)
			end
		end
	end
end

---Create a function that, when called, executes the provided function with
---the provided arguments.
---
---```
--- a(lfm.echo, "hey man")()
--
---```
---@param f function
---@vararg any
---@return function
function M.a(f, ...)
	local t = {...}
	if not f then
		return function() end
	elseif #t == 0 then
		return f
	elseif #t == 1 then
		local arg = t[1]
		return function()
			f(arg)
		end
	else
		return function()
			f(unpack(t))
		end
	end
end

---Create a function that, when called, feeds keys into the keyhandler.
---
---```
---feed(":quit")()
---```
---
---@vararg string
function M.feed(...)
	local keys = {...}
	return function()
		feedkeys(unpack(keys))
	end
end

---Check if an environment variable exists and is non-empty.
---@param var string
---@return boolean
function M.hasenv(var)
	local v = os.getenv(var)
	return v and v ~= "" or false
end

return M
