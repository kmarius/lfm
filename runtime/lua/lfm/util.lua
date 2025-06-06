local M = { _NAME = ... }

local lfm = lfm

local eval = lfm.eval
local feedkeys = lfm.feedkeys

---
---Construct a function that, when called, executes line as an expression.
---
---Example:
---```lua
---  local f = lfm.util.expr("cd ~")
--
---  f()
--
---  lfm.map("H", f, { desc = "Go home" })
---```
---
---@param line string
---@return function function
function M.expr(line)
	return function()
		eval(line)
	end
end

---
---Create a function that calls all functions given as the argument.
---
---Example:
---```lua
---  lfm.util.c(lfm.api.fm_load_clear, lfm.api.redraw)()
---```
---
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

---
---Create a function that, when called, executes the provided function with
---the provided arguments.
---
---Example:
---```lua
---  lfm.util.a(lfm.echo, "hey man")()
---```
---
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

---
---Create a function that, when called, feeds keys into the keyhandler.
---
---Example:
---```lua
---  lfm.util.feed(":quit")()
---  lfm.util.feed("cd ~", "<Enter>", ":quit")()
---```
---
---@param ... string
function M.feed(...)
	local keys = { ... }
	return function()
		feedkeys(unpack(keys))
	end
end

---
---Check if an environment variable exists and is non-empty.
---
---Example:
---```lua
---  if lfm.util.hasenv("TMUX") then
---    -- ...
---  end
---```
---
---@param var string
---@return boolean
function M.hasenv(var)
	local v = os.getenv(var)
	return v and v ~= "" or false
end

---
---Merges a table of default options into a given table. Applied recursively on subtables.
---opts table is mutated in the process and returned.
---
---Example:
---```lua
---  local dflt = { next = "n", prev = "p" }
---  local opts = { next = "<right>" }
---  opts = apply_default_options(opts, dflt)
---  assert(opts.next == "<right>")
---  assert(opts.prev == "p")
---```
---
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
