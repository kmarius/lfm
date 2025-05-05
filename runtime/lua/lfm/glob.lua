local M = { _NAME = ... }

local lfm = lfm

local api = lfm.api

local find = require("lfm.util").find
local selection_set = api.fm_selection_set
local basename = require("lfm.util").basename

-- TODO: should probably escape some special chars (on 2022-02-12)

---Convert a glob to a lua pattern.
---```lua
---   local pat = glob_to_pattern("*.txt")
---   string.match("/some/file.txt", pat)
---```
---@param glob string
---@return string pattern
function M.glob_to_pattern(glob)
	local res = string.gsub(glob, "%.", "%%.")
	res = string.gsub(res, "%*", ".*")
	res = string.gsub(res, "%?", ".?")
	return "^" .. res .. "$"
end

---Select all files in the current directory matching a glob.
---```lua
---    glob_select("*.txt")
---```
---@param glob string
function M.glob_select(glob)
	local pat = M.glob_to_pattern(glob)
	local sel = {}
	local match = string.match
	local insert = table.insert
	local basename = basename
	for _, file in ipairs(api.fm_current_dir().files) do
		if
			match(basename(file) --[[@as string]], pat)
		then
			insert(sel, file)
		end
	end
	selection_set(sel)
end

---Recursiv select all files matching a glob in the current directory and subdirectories.
---(probably breaks on symlink loops)
---```lua
---    glob_select_recursive("*.txt")
---```
---@param glob string
function M.glob_select_recursive(glob)
	local pat = M.glob_to_pattern(glob)
	local sel = {}
	local match = string.match
	local insert = table.insert
	local function filter(f)
		return match(f, pat)
	end
	for f in find(lfm.fn.getpwd(), filter) do
		insert(sel, f)
	end
	selection_set(sel)
end

function M._setup()
	-- GLOBSELECT mode
	local map = lfm.map
	local cmd = lfm.cmd
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
