local lfm = lfm

local getline = lfm.cmd.getline
local setline = lfm.cmd.setline
local commands = lfm.commands
local lfs = require("lfs")

local M = {}

---@param tok string token to complete
---@param line string the full command line
---@return string[] candidates
---@return string? separator
local function commands_provider(tok, line)
	local t = {}
	for c, _ in pairs(commands) do
		if string.sub(c, 1, #tok) == tok then
			table.insert(t, c)
		end
	end
	return t, " "
end

-- TODO: more options here (on 2022-02-11)
local options = {
	"hidden",
}

---Completion for the `set` command.
function M.options(tok, line)
	local t = {}
	if string.sub(tok, 1, 2) == "no" then
		for _, opt in pairs(options) do
			if string.sub("no"..opt, 1, #tok) == tok then
				table.insert(t, "no"..opt)
			end
		end
	elseif string.sub(tok, 1, 3) == "inv" then
		for _, opt in pairs(options) do
			if string.sub("inv"..opt, 1, #tok) == tok then
				table.insert(t, "inv"..opt)
			end
		end
	else
		for _, opt in pairs(options) do
			if string.sub(opt, 1, #tok) == tok then
				table.insert(t, opt)
			end
		end
	end
	return t, " "
end

local reset = false
local candidates = {}
local ind = 0
local provider = commands_provider

function M.reset()
	reset = true
end

local home = os.getenv("HOME")

---Complete directories.
function M.dirs(path)
	if path == "~" then
		return {"~"}, "/"
	end
	if path == ".." then
		return {".."}, "/"
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
	if lfs.attributes(dir) then
		for f in lfs.dir(dir) do
			if f ~= "." and f ~= ".." then
				if hidden == (string.sub(f, 1, 1) == ".") then
					if string.sub(f, 1, #prefix) == prefix then
						if lfs.attributes(dir .. f, "mode") == "directory" then
							table.insert(t, base..f)
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
function M.files(path)
	if path == "~" then
		return {"~"}, "/"
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
	if lfs.attributes(dir) then
		for f in lfs.dir(dir) do
			if f ~= "." and f ~= ".." then
				if hidden == (string.sub(f, 1, 1) == ".") then
					if string.sub(f, 1, #prefix) == prefix then
						if lfs.attributes(dir..f, "mode") == "directory" then
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

---Limit completion with `f` to `n` arguments.
---@param n number
---@param f function
---@return function
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
	local line = getline()

	local prefix, tok = lfm.fn.split_last(line)
	tok = lfm.fn.unquote_space(tok)

	if prefix == "" then
		provider = commands_provider
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
		setline(prefix..lfm.fn.quote_space(candidates[1])..sep)
		candidates = {}
	else
		ind = (ind - 1 + increment + #candidates) % #candidates + 1
		if candidates[ind] then
			setline(prefix..lfm.fn.quote_space(candidates[ind]))
		end
	end
end

---Show next completion entry.
function M.next()
	shownext(1)
end

---Show previous completion entry.
function M.prev()
	shownext(-1)
end

return M