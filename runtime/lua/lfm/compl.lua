local M = { _NAME = ... }

local lfm = lfm

local line_get = lfm.api.cmdline_line_get
local line_set = lfm.api.cmdline_line_set
local commands = lfm.commands
local stat = require("posix.sys.stat")
local dirent = require("posix.dirent")

---A completion function takes as first argument the current token to be completed
---and as second argument the full command line.
---It must return a table of completion candidates and optionally a separator,
---e.g. `"/"` or `"."` or even `""`.
---@alias Lfm.ComplFun fun(token: string, line: string): string[], string?

---@type Lfm.ComplFun
local function commands_provider(tok)
	local t = {}
	for c, _ in pairs(commands) do
		if string.sub(c, 1, #tok) == tok then
			table.insert(t, c)
		end
	end
	return t, " "
end

-- lazily generate fields from the luacats documentation file
local config_fields
local function get_config_fields()
	if not config_fields then
		config_fields = {}
		local path = lfm.fs.joinpath(lfm.paths.data_dir, "LuaCATS/lfm/o.lua")
		local enabled = false
		for line in io.lines(path, "*l") do
			if line == "---@class Lfm.Options" then
				enabled = true
			end
			if enabled then
				local field = line:match("^%-%-%-@field ([^ ]+)")
				table.insert(config_fields, field)
			end
		end
	end
	return config_fields
end

---Complete fields from the lfm namespace. Used to complete functions in command mode.
---@type Lfm.ComplFun
function M.table(_, line)
	local t = {}
	local prefix, suffix = string.match(line, "^(.*%.)(.*)")
	local tab = _G
	for s in string.gmatch(prefix, "([^.]*).") do
		if not tab[s] then
			return {}, "."
		end
		tab = tab[s]
	end
	if type(tab) ~= "table" then
		-- TODO: handle config (on 2022-03-01)
		return {}, "."
	end
	for e, _ in pairs(tab) do
		if string.sub(e, 1, #suffix) == suffix then
			table.insert(t, prefix .. e)
		end
	end
	if prefix == "lfm.o." then
		for _, field in ipairs(get_config_fields()) do
			if string.sub(field, 1, #suffix) == suffix then
				table.insert(t, prefix .. field)
			end
		end
	end
	return t, "."
end

local reset = false
local candidates = {}
local ind = 0
---@type Lfm.ComplFun
local provider = commands_provider

function M.reset()
	reset = true
end

local home = os.getenv("HOME")

local function is_dir(path)
	local s = stat.stat(path)
	return s and stat.S_ISDIR(s.st_mode) == 1
end

---Complete directories.
---```lua
---    local compl_fun = lfm.compl.dirs
---    lfm.register_command("cool-command", { compl = compl_fun })
---```
---@type Lfm.ComplFun
function M.dirs(path)
	if path == "~" then
		return { "~" }, "/"
	end
	if path == ".." then
		return { ".." }, "/"
	end
	local t = {}
	local dir, prefix = string.match(path, "^(.*/)(.*)")
	local base = dir
	if not dir then
		dir, base, prefix = "./", "", path
	end
	if string.sub(dir, 1, 1) == "~" then
		dir = home .. string.sub(dir, 2)
	end
	local hidden = string.match(prefix, "^%.") ~= nil
	if stat.stat(dir) then
		for f in dirent.files(dir) do
			if f ~= "." and f ~= ".." then
				if hidden == (string.sub(f, 1, 1) == ".") then
					if string.sub(f, 1, #prefix) == prefix then
						if is_dir(dir .. f) then
							table.insert(t, base .. f)
						end
					end
				end
			end
		end
	end
	return t, "/"
end

-- maybe show dirs before directories
-- files could be additionaly filtered with a function

---Complete files.
---```lua
---    local compl_fun = lfm.compl.files
---    lfm.register_command("cool-command", { compl = compl_fun })
---```
---@type Lfm.ComplFun
function M.files(path)
	if path == "~" then
		return { "~" }, "/"
	end
	local t = {}
	local dir, prefix = string.match(path, "^(.*/)(.*)")
	local base = dir
	if not dir then
		dir, base, prefix = "./", "", path
	end
	if string.sub(dir, 1, 1) == "~" then
		dir = home .. string.sub(dir, 2)
	end
	local hidden = string.match(prefix, "^%.") ~= nil
	if stat.stat(dir) then
		for f in dirent.files(dir) do
			if f ~= "." and f ~= ".." then
				if hidden == (string.sub(f, 1, 1) == ".") then
					if string.sub(f, 1, #prefix) == prefix then
						if is_dir(dir .. f) then
							table.insert(t, base .. f .. "/")
						else
							table.insert(t, base .. f)
						end
					end
				end
			end
		end
	end
	return t, (#t == 1 and string.sub(t[1], #t[1]) == "/") and "" or " "
end

---Limit completion of completion function `f` to `n` arguments.
---
---Complete a single file argument:
---```lua
---    local compl_fun = lfm.compl.limit(1, lfm.compl.files)
---    lfm.register_command("cool-command", { compl = compl_fun })
---```
---@param n number
---@param f Lfm.ComplFun
---@return Lfm.ComplFun
function M.limit(n, f)
	return function(tok, line)
		local _, toks = lfm.fn.tokenize(line)
		if tok == "" and #toks >= n or tok ~= "" and #toks > n then
			return {}
		else
			return f(tok, line)
		end
	end
end

local function shownext(increment)
	local line = line_get()

	local prefix, tok = lfm.fn.split_last(line)
	tok = lfm.fn.unquote_space(tok)

	if prefix == "" then
		if string.match(tok, "^lfm%.") then
			provider = M.table
		else
			provider = commands_provider
		end
	else
		local cmd = string.match(prefix, "^([^%s]*)")
		if commands[cmd] and commands[cmd].compl then
			provider = commands[cmd].compl
		else
			provider = commands_provider
			return
		end
	end
	local sep
	if reset or #candidates == 0 or tok ~= candidates[ind] then
		reset = false
		---@diagnostic disable-next-line: need-check-nil
		candidates, sep = provider(tok, line)
		ind = 0
		local contained = false
		for _, e in pairs(candidates) do
			if e == tok then
				contained = true
				break
			end
		end
		if #candidates == 0 then
			return
		end
		table.sort(candidates)
		if #candidates > 1 and not contained then
			table.insert(candidates, tok)
		end
	end
	if #candidates == 1 then
		line_set(prefix .. lfm.fn.quote_space(candidates[1]) .. sep)
		candidates = {}
	else
		ind = (ind - 1 + increment + #candidates) % #candidates + 1
		if candidates[ind] then
			line_set(prefix .. lfm.fn.quote_space(candidates[ind]))
		end
	end
end

---Show next completion entry.
---```lfm
---    lfm.compl.next()
---```
function M.next()
	shownext(1)
end

---Show previous completion entry.
---```lfm
---    lfm.compl.prev()
---```
function M.prev()
	shownext(-1)
end

return M
