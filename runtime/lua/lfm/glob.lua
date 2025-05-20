local M = { _NAME = ... }

---A set of globs (patterns)
---@class Glob.GlobPatterns

---@class Glob.Opts
---@field full_paths? boolean List files with their full paths

local lfm = lfm

local api = lfm.api
local fs = lfm.fs

local dirent = require("posix.dirent")

---Match files against a single glob
---@param path string
---@param glob string
---@param opts? Glob.Opts
---@return string[]
local function glob_files_single(path, glob, opts)
	opts = opts or {}
	local pattern, match_dot = M.to_pattern(glob)
	local files = {}
	for file in dirent.files(path) do
		if file ~= "." and file ~= ".." then
			if (file:sub(1, 1) == ".") == match_dot then
				if string.match(file, pattern) then
					if opts.full_paths then
						file = path .. "/" .. file
					end
					table.insert(files, file)
				end
			end
		end
	end
	return files
end

---Match files against a multiple globs
---@param path string
---@param globs Glob.GlobPatterns
---@param opts? Glob.Opts
---@return string[]
local function glob_files_multiple(path, globs, opts)
	opts = opts or {}
	local patterns = M.to_patterns(globs)
	local files = {}
	for file in dirent.files(path) do
		if file ~= "." and file ~= ".." then
			for pattern, match_dot in pairs(patterns) do
				if (file:sub(1, 1) == ".") == match_dot then
					if string.match(file, pattern) then
						if opts.full_paths then
							file = path .. "/" .. file
						end
						table.insert(files, file)
					end
				end
			end
		end
	end
	return files
end

---Convert a glob into a pattern. Can not contain "/". Second return value indicates wether it should match dot files.
---```lua
---   local pat = glob.to_pattern("*.txt")
---   string.match("/some/file.txt", pat)
---```
---@param glob string
---@return string pattern
---@return boolean
function M.to_pattern(glob)
	if glob:match("/") then
		error('"/" in glob')
	end
	local match_dot = glob:sub(1, 1) == "."
	glob = string.gsub(glob, "%.", "%%.")
	glob = string.gsub(glob, "*", ".*")
	glob = string.gsub(glob, "?", ".")
	return "^" .. glob .. "$", match_dot
end

---Convert an array of globs into a set of patterns
---@param globs string[]
---@return Glob.GlobPatterns
function M.to_patterns(globs)
	assert(type(globs) == "table")
	local patterns = {}
	for _, g in ipairs(globs) do
		local pattern, match_dot = M.to_pattern(g)
		patterns[pattern] = match_dot
	end
	return patterns
end

---Check if a file matches a set of globs
---@param file string
---@param globs Glob.GlobPatterns|string
---@param match_dot? boolean
---@return boolean
function M.matches(file, globs, match_dot)
	if type(globs) == "string" then
		return match_dot == (file:sub(1, 1) == ".") and file:match(globs)
	end
	for pattern, match_dot in pairs(globs) do
		if match_dot == (file:sub(1, 1) == ".") and file:match(pattern) then
			return true
		end
	end
	return false
end

---Get files matching one or more globs in a directory.
---@param path string The directory.
---@param glob string|string[] The globs.
---@param opts? Glob.Opts
---@return string[]
function M.files(path, glob, opts)
	if type(glob) == "table" then
		return glob_files_multiple(path, glob, opts)
	else
		return glob_files_single(path, glob, opts)
	end
end

---
---Select all files in the current directory matching a glob.
---
---Example:
---```lua
---  glob_select("*.txt")
---```
---
---@param glob string
function M.glob_select(glob)
	local files = glob_files_single(".", glob, { full_paths = true })
	api.fm_selection_set(files)
end

---
---Recursiv select all files matching a glob in the current directory and subdirectories.
---(probably breaks on symlink loops)
---
---Example:
---```lua
---  glob_select_recursive("*.txt")
---```
---
---@param glob string
function M.glob_select_recursive(glob)
	local pattern, match_dot = M.to_pattern(glob)
	local function filter(file)
		return M.matches(file, pattern, match_dot)
	end
	local files = fs.find(filter, { path = lfm.fn.getpwd(), limit = 1000000, follow = true })
	api.fm_selection_set(files)
end

function M._setup()
	-- GLOBSELECT mode
	local map = lfm.map
	local a = require("lfm.util").a

	local mode = {
		name = "glob-select",
		input = true,
		prefix = "glob-select: ",
		on_return = function()
			lfm.mode("normal")
		end,
		on_esc = function()
			api.fm_selection_set({})
		end,
		on_change = function()
			require("lfm.glob").glob_select(api.cmdline_line_get())
		end,
	}

	lfm.register_mode(mode)
	map("*", a(lfm.mode, mode.name), { desc = "glob-select" })

	lfm.register_command(
		"glob-select",
		require("lfm.glob").glob_select,
		{ tokenize = false, desc = "Select files in the current directory matching a glob." }
	)

	lfm.register_command(
		"glob-select-rec",
		require("lfm.glob").glob_select_recursive,
		{ tokenize = false, desc = "Select matching a glob recursively." }
	)
end

return M
