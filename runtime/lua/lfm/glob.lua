local lfm = lfm

local fm = lfm.fm
local find = require("lfm.util").find
local selection_set = fm.selection_set
local basename = require("lfm.util").basename
local M = {}

-- TODO: should probably escape some special chars (on 2022-02-12)

--Convert a glob to a lua pattern.
---@param glob string
---@return string pattern
function M.glob_to_pattern(glob)
	local res = string.gsub(glob, "%.", "%%.")
	res = string.gsub(res, "%*", ".*")
	res = string.gsub(res, "%?", ".?")
	return "^" .. res .. "$"
end

--Select all files in the current directory matching a glob.
---@param glob string
---@return string[] files
function M.glob_select(glob)
	local pat = M.glob_to_pattern(glob)
	local sel = {}
	local match = string.match
	local insert = table.insert
	local basename = basename
	for _, file in pairs(fm.current_dir().files) do
		if match(basename(file), pat) then
			insert(sel, file)
		end
	end
	selection_set(sel)
end

--Recursiv select all files matching a glob in the current directory and subdirectories.
---(probably breaks on loops)
---@param glob string
---@return string[] files
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

M.mode_glob_select = {
	name = "glob-select",
	input = true,
	prefix = "glob-select: ",
	on_return = function()
		lfm.mode("normal")
	end,
	on_esc = function()
		fm.selection_set({})
	end,
	on_change = function()
		M.glob_select(lfm.cmd.line_get())
	end,
}

return M
