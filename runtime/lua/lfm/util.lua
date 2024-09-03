local M = { _NAME = ... }

local lfm = lfm

local stat = require("posix.sys.stat")
local dirent = require("posix.dirent")

local eval = lfm.eval
local feedkeys = lfm.feedkeys

---Get dirname of path.
---```lua
---    lfm.util.dirname("/home/john/file.txt") == "/home/john"
---```
---@param path string
---@return string
---@overload fun(path: nil): nil
function M.dirname(path)
	if not path then
		return nil
	end
	if not string.match(path, "^/") then
		path = "./" .. path
	end
	return (string.gsub(path, "/[^/]*$", ""))
end

---Get basename of path.
---```lua
---    lfm.util.basename("/home/john/file.txt") == "file.txt"
---```
---@param path string
---@return string
---@overload fun(path: nil): nil
function M.basename(path)
	if not path then
		return nil
	end
	return (string.gsub(path, "^.*/", ""))
end

---Split a file into its base name and extension
---```lua
---    local a, ext = lfm.util.file_split("file.txt")
---    assert(a == "file" and ext == "txt")
---
---    local a, ext = lfm.util.file_split(".hidden")
---    assert(a == ".hidden" and ext ==  nil)
---```
---@param file string
---@return string name
---@return string? extension
---@overload fun(path: nil): nil
function M.file_split(file)
	if not file then
		return nil
	end
	local i, j = string.find(file, "%.[^.]*$")
	if not i or i == 1 then
		return file
	else
		return string.sub(file, 1, i - 1), string.sub(file, i + 1, j)
	end
end

---Construct a function that, when called, executes line as an expression.
---```lua
---    local f = lfm.util.expr("cd ~")
--
---    f()
--
---    lfm.map("H", f, { desc = "Go home" })
---```
---@param line string
---@return function function
function M.expr(line)
	return function()
		eval(line)
	end
end

---Create a function that calls all functions given as the argument.
---```lua
---    lfm.util.c(lfm.fm.load_clear, lfm.ui.redraw)()
---```
---@param ... function
---@return function
function M.c(...)
	local t = { ... }
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
---```lua
---    lfm.util.a(lfm.echo, "hey man")()
---```
---@param f function
---@param ... any
---@return function
function M.a(f, ...)
	local t = { ... }
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
---```lua
---    lfm.util.feed(":quit")()
---    lfm.util.feed("cd ~", "<Enter>", ":quit")()
---```
---@param ... string
function M.feed(...)
	local keys = { ... }
	return function()
		feedkeys(unpack(keys))
	end
end

---Check if an environment variable exists and is non-empty.
---```lua
---    if lfm.util.hasenv("TMUX") then
---      -- ...
---    end
---````
---@param var string
---@return boolean
function M.hasenv(var)
	local v = os.getenv(var)
	return v and v ~= "" or false
end

---Recursive dirwalk with an optional filter.
---```lua
---    for _, file in lfm.util.find(".", function(f) return string.sub(f, 1, 1) ~= "." end) do
---        print(file)
---    end
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

---Merges a table of default options into a given table. Applied recursively on subtables.
---opts table is mutated in the process and returned.
---```lua
---    local dflt = { next = "n", prev = "p" }
---    local opts = { next = "<right>" }
---    opts = apply_default_options(opts, dflt)
---    assert(opts.next == "<right>")
---    assert(opts.prev == "p")
---```
---@param opts table Table of options
---@param dflt table Table of default options
---@return table
local function apply_default_options(opts, dflt)
	opts = opts or {}
	for key, value in pairs(dflt) do
		if type(value) == "table" then
			opts[key] = apply_default_options(opts[key], value)
		else
			opts[key] = opts[key] or value
		end
	end
	return opts
end
M.apply_default_options = apply_default_options

return M
